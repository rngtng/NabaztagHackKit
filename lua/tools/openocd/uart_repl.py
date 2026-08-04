#!/usr/bin/env python3
"""Drive the firmwareV2 Lua REPL over UART0, on the Pi's /dev/serial0 (#207).

Runs ON THE PI (it owns the serial line wired to the rabbit's UART). flash.py
--uart ships it here with the OpenOCD configs and runs it over ssh right after a
JTAG flash. It optionally feeds an input file to the REPL, then reads the console
until the app prints the done-marker or a timeout elapses.

Flow control: the link is 115200 8N1 with NO hardware flow control and the device
polls a 16-byte RX FIFO, draining it only while it is reading a line/frame - NOT
during the gaps where it does other work. Two such gaps drop bytes for #LC
bytecode frames (#128): after the `#LC:<len>:<sum>` header the device runs malloc(len)
before the hex loop, and after a frame it runs the chunk + prints before reading
the next header. A blind byte burst overflows the 16-byte FIFO in either gap. So
within one frame, input is paced one byte at a time, and after every newline we
pause (`line_gap`) to let the device finish that gap.

#276: a FIXED inter-frame gap is not enough. If a chunk's own execution blocks
(nab.play() decoding a real clip can run for seconds) the device does not touch
UART at all for that whole stretch - dispatch_events during a blocking call
never reads input, only button/rfid/tick - so any fixed gap shorter than that
blocking call still overflows the 16-byte FIFO the moment the next frame's bytes
start arriving. Reproduced on apps/mic-test.lua: corruption clusters right after
the two nab.play() lines, never elsewhere. Fixed by feeding per-FRAME (not one
blind byte stream): send one frame's bytes (paced, as before), then wait for the
device's "> " prompt to reappear - which only happens once repl() has finished
running that chunk and is back at sh_gets() - before sending the next frame's
header. This bounds each inter-frame wait to exactly as long as that chunk
actually took, however long that is, instead of guessing a constant.

EOF: the firmware treats EOT (0x04, what Ctrl-D sends) as end-of-input - that is
what ends the REPL loop and makes it print <<FV_DONE>>. So a fed script is
followed by a single EOT byte.
"""
import argparse
import os
import select
import subprocess
import sys
import termios
import time

DEV = "/dev/serial0"
EOT = b"\x04"


def stop_getty():
    """Free the serial line from the login console (re-enabled on reboot)."""
    for unit in ("serial-getty@ttyAMA0", "serial-getty@ttyS0"):
        subprocess.run(["systemctl", "stop", unit],
                       stderr=subprocess.DEVNULL, check=False)


def open_port(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                          # iflag: raw
    a[1] = 0                                          # oflag: raw
    a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    a[3] = 0                                          # lflag: raw (no echo/canon)
    a[4] = termios.B115200                             # ispeed
    a[5] = termios.B115200                             # ospeed
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def drain(fd, out):
    """Non-blocking: pull whatever RX bytes are waiting into out."""
    try:
        while True:
            d = os.read(fd, 256)
            if not d:
                break
            out.extend(d)
    except BlockingIOError:
        pass


PROMPT = b"> "


def split_frames(payload: bytes) -> list[bytes]:
    """Split a replpipe.py stream into its individual #LC frames. A frame is a
    "#LC:<len>:<sum>\\n" header line (len = chunk bytes in decimal, sum = the
    #298 Fletcher-32 of those bytes as 8 hex digits) plus every following line
    up to (but not including) the next header line or EOF - i.e. exactly the
    bytes one load_lc_frame() call on the device consumes.

    The split only prefix-matches "#LC:" and never parses the header, so the
    #298 field costs nothing here - but two readers of these comments have now
    missed that field (this file, and tools/simui/app.py, where it was a live
    regression: #308). They are the description of the wire format nearest the
    hardware path, so they say the whole format."""
    lines = payload.splitlines(keepends=True)
    frames, cur = [], bytearray()
    for line in lines:
        if line.startswith(b"#LC:") and cur:
            frames.append(bytes(cur))
            cur = bytearray()
        cur += line
    if cur:
        frames.append(bytes(cur))
    return frames


def send_paced(fd, data: bytes, byte_delay, line_gap, out):
    """Write `data` one byte at a time (flow control), pausing extra after each
    newline for the device's malloc/hex-loop gap. Drains RX between bytes so
    output is never missed while we are busy sending."""
    for byte in data:
        os.write(fd, bytes([byte]))
        time.sleep(byte_delay)
        drain(fd, out)
        if byte == 0x0A:
            time.sleep(line_gap)
            drain(fd, out)


def wait_for_prompt(fd, out, timeout):
    """Block until a FRESH "> " prompt appears in `out` - i.e. repl() has
    finished running the frame just sent and is back at sh_gets(), ready for
    the next one (#276: a chunk's own blocking work, e.g. nab.play(), can run
    far longer than any fixed inter-frame gap, and the device does not touch
    UART at all meanwhile - waiting for its own readiness signal instead of
    guessing a duration is what makes this correct for an arbitrary script).
    `out` already ends with the *previous* prompt when this is called (the
    device printed it and is now silently receiving raw bytes, with no echo),
    so simply checking endswith(PROMPT) would fire immediately on that stale
    tail with nothing new having happened - genuinely new bytes must arrive
    first. Bounded so a truly wedged chunk does not hang forever."""
    baseline = len(out)
    t = time.monotonic()
    while time.monotonic() - t < timeout:
        drain(fd, out)
        if len(out) > baseline and out.endswith(PROMPT):
            return True
        time.sleep(0.01)
    return False


def relay(fd, done, byte_delay, line_gap, idle_timeout):
    """Bidirectional pass-through for the live REPL (#128): our stdin -> serial
    (paced for flow control), serial -> our stdout. luash.py on the workstation
    drives us over ssh and does the compile-per-line; we only move bytes. It
    sends EOT then closes stdin to end; we drain the reply until the done-marker
    or an idle gap, then exit."""
    stdin_fd = sys.stdin.buffer.fileno()
    stdin_open = True
    seen = bytearray()
    last_rx = time.monotonic()
    while True:
        watch = [fd] + ([stdin_fd] if stdin_open else [])
        r, _, _ = select.select(watch, [], [], 0.1)
        if fd in r:
            try:
                d = os.read(fd, 256)
            except BlockingIOError:
                d = b""
            if d:
                sys.stdout.buffer.write(d)
                sys.stdout.buffer.flush()
                seen += d
                last_rx = time.monotonic()
        if stdin_open and stdin_fd in r:
            chunk = os.read(stdin_fd, 4096)
            if not chunk:
                stdin_open = False   # luash closed stdin (EOT already in-stream)
            else:
                for byte in chunk:                # paced TX (16-byte RX FIFO)
                    os.write(fd, bytes([byte]))
                    time.sleep(byte_delay)
                    if byte == 0x0A:              # newline: cover the device's
                        time.sleep(line_gap)      # no-read gap (malloc / run+print)
        if not stdin_open:
            if done in seen or time.monotonic() - last_rx > idle_timeout:
                break


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dev", default=DEV, help=f"serial device (default {DEV})")
    ap.add_argument("--input", default=None,
                    help="file fed to the REPL (paced); omit to just read boot output")
    ap.add_argument("--done", default="<<FV_DONE>>",
                    help="marker that ends the read early (default <<FV_DONE>>)")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="max seconds to read after feeding input (default 30)")
    ap.add_argument("--byte-delay", type=float, default=0.003,
                    help="per-byte send delay for flow control (default 0.003s)")
    ap.add_argument("--line-gap", type=float, default=0.08,
                    help="extra pause after each newline so the device can finish "
                         "its no-read gap (malloc after an #LC header, run+print "
                         "after a frame) before more bytes arrive (default 0.08s)")
    ap.add_argument("--boot-wait", type=float, default=1.5,
                    help="seconds to let the app boot to its prompt before feeding")
    ap.add_argument("--prompt-timeout", type=float, default=20.0,
                    help="max seconds to wait for the \"> \" prompt between "
                         "frames before giving up on that frame and sending "
                         "the next one anyway (default 20s, #276)")
    ap.add_argument("--relay", action="store_true",
                    help="live REPL: bidirectional stdin<->serial pass-through "
                         "(paced), driven by luash.py over ssh. See tools/luac/luash.py")
    a = ap.parse_args()

    stop_getty()
    fd = open_port(a.dev)
    out = bytearray()
    done = a.done.encode()

    if a.relay:
        relay(fd, done, a.byte_delay, a.line_gap, a.timeout)
        os.close(fd)
        sys.exit(0)

    # Let the app boot and reach its "> " prompt before feeding input.
    t = time.monotonic()
    while time.monotonic() - t < a.boot_wait:
        drain(fd, out)
        time.sleep(0.02)

    if a.input:
        with open(a.input, "rb") as fh:
            payload = fh.read()
        # #276: send one frame at a time, waiting for the device's own "> "
        # prompt between frames instead of a fixed inter-frame gap - a chunk's
        # blocking work (nab.play(), a wifi join) can run far longer than any
        # constant, and the device touches no UART bytes while it does.
        for frame in split_frames(payload):
            send_paced(fd, frame, a.byte_delay, a.line_gap, out)
            if not wait_for_prompt(fd, out, a.prompt_timeout):
                sys.stderr.write(
                    f"uart_repl: no prompt within {a.prompt_timeout}s after a "
                    "frame - sending the next one anyway\n")
        os.write(fd, EOT)                             # EOT -> EOF -> REPL ends

    # Read the console until the done-marker or the timeout.
    t = time.monotonic()
    while time.monotonic() - t < a.timeout:
        drain(fd, out)
        if done in out:
            break
        time.sleep(0.02)

    sys.stdout.buffer.write(bytes(out))
    sys.stdout.flush()
    os.close(fd)
    # With input we expect the done-marker; without, capturing boot output is success.
    sys.exit(0 if (done in out or not a.input) else 1)


if __name__ == "__main__":
    main()
