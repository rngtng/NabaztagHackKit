#!/usr/bin/env python3
"""Generate inc/tone_mp3.h - the tiny embedded MP3 tone behind nab.tone().

The original header was a one-off `lame -b48` blob with two audible defects:
the un-faded sine ends mid-cycle, so the MP3 encoder rings at the cut
(a "distortion at the end"), and the file opened with a LAME Xing/Info
metadata frame the VS1003 decodes as a garbage frame. This script makes the
asset reproducible and clean:

  * 880 Hz sine, 16-bit mono 32 kHz, raised-cosine fade-in/out - the signal
    reaches zero before the file ends, so the encoder has nothing to ring on;
  * trailing silence so the decoder's last granules are silent;
  * lame -t: no Xing/Info frame - the first frame is real audio.

Runs inside the firmware image (lame + python3 are baked in):
  task lua:firmware:gen:tone      # -> inc/tone_mp3.h
"""

import argparse
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

RATE = 32000
TONE_MS = 250
FADE_MS = 20
TAIL_MS = 50
BITRATE = 48
DRIVE = 2.5  # tanh soft-saturation: near-square RMS + harmonics the small
             # speaker projects (a pure 880 Hz sine plays noticeably quiet),
             # but smooth-edged so the encoder has no hard transition to ring on


def synth(freq: float) -> bytes:
    n_tone = RATE * TONE_MS // 1000
    n_fade = RATE * FADE_MS // 1000
    n_tail = RATE * TAIL_MS // 1000
    samples = []
    for i in range(n_tone):
        a = 0.98
        if i < n_fade:  # raised-cosine fade-in
            a *= 0.5 - 0.5 * math.cos(math.pi * i / n_fade)
        if i >= n_tone - n_fade:  # ...and fade-out to exactly zero
            a *= 0.5 - 0.5 * math.cos(math.pi * (n_tone - 1 - i) / n_fade)
        s = math.tanh(DRIVE * math.sin(2 * math.pi * freq * i / RATE)) / math.tanh(DRIVE)
        samples.append(int(a * 32767 * s))
    samples.extend(0 for _ in range(n_tail))
    return struct.pack("<%dh" % len(samples), *samples)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("-o", "--out", type=Path, default=Path("inc/tone_mp3.h"),
                   help="a .h emits the C header; a .mp3 dumps the raw file (probes)")
    p.add_argument("--freq", type=float, default=880.0)
    args = p.parse_args()
    out = args.out

    with tempfile.TemporaryDirectory() as td:
        wav, mp3 = Path(td, "tone.wav"), Path(td, "tone.mp3")
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(synth(args.freq))
        subprocess.run(
            ["lame", "--quiet", "-b", str(BITRATE), "-t", str(wav), str(mp3)],
            check=True,
        )
        data = mp3.read_bytes()

    if out.suffix == ".mp3":
        out.write_bytes(data)
        print("tonegen: %s (%d bytes MP3, %d Hz)" % (out, len(data), args.freq))
        return

    lines = [
        "/* Embedded %dms %dHz MP3 tone for nab.tone() - GENERATED, do not edit:" % (TONE_MS, int(args.freq)),
        " *   task lua:firmware:gen:tone   (tools/tonegen.py: faded sine -> lame -b%d -t)" % BITRATE,
        " * Fade-out + trailing silence keep the tone's end click-free; -t drops the",
        " * Xing/Info frame the VS1003 would decode as garbage. MP3 because the",
        " * VS1003B on this board decodes MP3, NOT raw PCM WAV. */",
        "static const unsigned char nab_tone_mp3[%d] = {" % len(data),
    ]
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join(str(b) for b in data[i : i + 16]) + ",")
    lines.append("};")
    out.write_text("\n".join(lines) + "\n")
    print("tonegen: %s (%d bytes MP3)" % (out, len(data)))


if __name__ == "__main__":
    main()
