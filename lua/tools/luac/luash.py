#!/usr/bin/env python3
"""Interactive Lua REPL client for the bytecode-only firmware (#128).

The rabbit has no on-device parser, so it cannot compile source it is typed -
it only runs luac bytecode (#LC frames). This client keeps a live, interactive
REPL alive by moving the compile step to the host: it reads a line you type,
compiles it off-device through the luac Docker image (the same one replpipe.py
uses, expression-first so a bare `1+1` echoes), frames it as #LC, and writes it
to the device console. Device output (results + the `> ` prompt) is relayed
straight back to your terminal, so it reads like a normal prompt.

Transport-abstract: the "device console" is just a child process whose stdin is
the console input and whose stdout is the console output. The Taskfile wires it:

  * simulator:  docker run -i <sim-image> --interactive /mnt/lua.elf
  * hardware:   ssh <pi> sudo python3 uart_repl.py --relay   (paces to /dev/serial0)

so this file needs to know nothing about Docker vs. UART. Usage:

  luash.py [--image nabaztag-sdk-luac] -- <child command...>

Ctrl-D ends the session: it sends EOT (0x04), which the firmware treats as EOF -
the REPL loop exits and prints <<FV_DONE>>. Python stdlib only.
"""
import argparse
import os
import subprocess
import sys
import threading

from replpipe import luac_compile  # same host luac image + invocation

EOT = b"\x04"
DEBUG = bool(os.environ.get("LUASH_DEBUG"))


def dbg(msg):
    if DEBUG:
        sys.stderr.write(f"[luash] {msg}\n")
        sys.stderr.flush()


def compile_line(line: bytes, image: str):
    """Compile one REPL line, expression-first (mirrors the old device load_line
    and replpipe.frame_lua_source): try `return <line>` so a bare expression
    evaluates and echoes, then the line verbatim. Returns (chunk, error)."""
    chunk, _ = luac_compile(b"return " + line, image)
    if chunk is not None:
        return chunk, ""
    return luac_compile(line, image)  # (chunk|None, stderr)


def send_frame(w, chunk: bytes) -> None:
    """Write one #LC frame to the console input: header line + hex payload
    wrapped at 64 cols (the format load_lc_frame decodes). Bytes, not text."""
    w.write(f"#LC:{len(chunk)}\n".encode())
    hexs = chunk.hex()
    for i in range(0, len(hexs), 64):
        w.write((hexs[i:i + 64] + "\n").encode())
    w.flush()


def relay_output(child_stdout, done: threading.Event) -> None:
    """Pump the device console straight to our stdout until the child closes."""
    n = 0
    try:
        while True:
            data = child_stdout.read(1)
            if not data:
                break
            n += 1
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
    finally:
        dbg(f"reader saw {n} bytes from the device console")
        done.set()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", default="nabaztag-sdk-luac",
                    help="luac Docker image (default: nabaztag-sdk-luac)")
    ap.add_argument("child", nargs=argparse.REMAINDER,
                    help="-- <command> whose stdin/stdout is the device console")
    args = ap.parse_args()

    child_cmd = args.child[1:] if args.child and args.child[0] == "--" else args.child
    if not child_cmd:
        sys.exit("luash: need a child command after `--` (the device console)")

    child = subprocess.Popen(child_cmd, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE)
    done = threading.Event()
    reader = threading.Thread(target=relay_output, args=(child.stdout, done),
                              daemon=True)
    reader.start()

    try:
        # One frame per typed line; the device prints results + prompt itself,
        # relayed by the reader thread, so we only feed input here.
        for line in sys.stdin:
            if done.is_set():
                dbg("child closed before send; stopping")
                break
            chunk, err = compile_line(line.encode(), args.image)
            if chunk is None:
                # Compile error: nothing to run, so the device stays at its
                # prompt (it emitted no new `> `). Show the error and re-prompt
                # locally to keep one prompt per line.
                dbg(f"compile error for {line.rstrip()!r}")
                sys.stdout.write(err)
                sys.stdout.write("> ")
                sys.stdout.flush()
                continue
            try:
                dbg(f"sending frame ({len(chunk)} B) for {line.rstrip()!r}")
                send_frame(child.stdin, chunk)
            except BrokenPipeError:
                dbg("BrokenPipe writing frame; stopping")
                break
    except KeyboardInterrupt:
        pass
    finally:
        # EOT -> firmware EOF -> REPL exits + prints <<FV_DONE>>.
        try:
            child.stdin.write(EOT)
            child.stdin.flush()
            child.stdin.close()
        except (BrokenPipeError, OSError):
            pass
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
        done.wait(timeout=5)
    return child.returncode or 0


if __name__ == "__main__":
    sys.exit(main())
