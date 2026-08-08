#!/usr/bin/env python3
"""Generate inc/tone_midi.h - the tiny embedded tone behind nab.tone() (#331).

The asset used to be a `lame -b48` MP3 of a faded 880 Hz sine: 2,160 B of a
124 KB image with ~7.5 KB free, i.e. 28% of all remaining headroom spent on a
beep. But the VS1003B decodes MIDI natively, so the same audible result is a
45-byte Standard MIDI File - a codec swap, not a feature cut, and nab.tone()
keeps the property that earns its keep: still a resident byte string handed
straight to nab.play, so it is the one audio asset that works with zero libs
loaded and answers "is the codec alive at all" at a bare REPL.

The file we emit is deliberately the same construction ../lib/audio/midi.lua
builds for a Lua-side jingle - same DIVISION/TEMPO/PROGRAM/VELOCITY, same
event order - so the resident tone and `audio.midi.note("A5", 250)` are the
same 45 bytes and cannot drift in pitch or tempo conventions. No code is
shared across the seam, only the constants below and the comments pointing
each way; test/host/tone_test.c pins the bytes from the SMF spec, and
../lib/audio/test/test_midi.lua asserts the Lua side still agrees with them.

  task lua:firmware:gen:tone            # -> inc/tone_midi.h (needs no encoder)

--format mp3 regenerates the MP3 predecessor (`lame`, baked into the firmware
image) into inc/tone_mp3.h. It is kept because the MIDI swap's hardware gate -
an SMF actually audible on the rig - is the one claim this repo cannot make
from a container; if the rig says otherwise, the old asset comes back with one
command rather than an archaeology session.
"""

import argparse
import math
import re
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

# --- the tone itself ---------------------------------------------------------
NOTE = "A5"      # 880 Hz, the pitch the MP3 predecessor synthesised
TONE_MS = 250

# --- MIDI: mirrors lib/audio/midi.lua's module constants exactly -------------
DIVISION = 480   # ticks per quarter note
TEMPO = 500000   # microseconds per quarter note (= 120 bpm)
PROGRAM = 9      # GM 10, glockenspiel - the V1 jingle voice
VELOCITY = 100

# --- MP3 (--format mp3 only) -------------------------------------------------
RATE = 32000
FADE_MS = 20
TAIL_MS = 50
BITRATE = 48
DRIVE = 2.5  # tanh soft-saturation: near-square RMS + harmonics the small
             # speaker projects (a pure 880 Hz sine plays noticeably quiet),
             # but smooth-edged so the encoder has no hard transition to ring on

SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_num(name: str) -> int:
    """"A5" / "F#5" / "Bb3" -> MIDI note number (C4 = 60). midi.lua's midi.num."""
    m = re.fullmatch(r"([A-G])([#b]?)(-?\d+)", name, re.IGNORECASE)
    if not m:
        raise ValueError("not a note name: %r" % name)
    letter, acc, octave = m.group(1).upper(), m.group(2), int(m.group(3))
    return (SEMI[letter] + (octave + 1) * 12
            + (1 if acc == "#" else -1 if acc else 0))


def varlen(n: int) -> bytes:
    """SMF variable-length quantity (7 bits per byte, high bit = more follow)."""
    out = bytes([n & 0x7F])
    n >>= 7
    while n > 0:
        out = bytes([(n & 0x7F) | 0x80]) + out
        n >>= 7
    return out


def ticks(ms: int) -> int:
    """ms -> ticks at the tempo actually written into the file (midi.lua's ticks)."""
    return ms * DIVISION // (TEMPO // 1000)


def synth_smf(note: str, ms: int) -> bytes:
    """A Format-0 SMF of one note: the smallest file a decoder accepts."""
    num = note_num(note)
    track = (
        varlen(0) + b"\xff\x51\x03" + struct.pack(">I", TEMPO)[1:]  # FF 51 03 tempo
        + varlen(0) + bytes([0xC0, PROGRAM])                        # program change
        + varlen(0) + bytes([0x90, num & 0x7F, VELOCITY])           # note on
        + varlen(ticks(ms)) + bytes([0x80, num & 0x7F, 0x40])       # note off
        + varlen(0) + b"\xff\x2f\x00"                               # end of track
    )
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, DIVISION)
            + b"MTrk" + struct.pack(">I", len(track)) + track)


def synth_pcm(freq: float) -> bytes:
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


def synth_mp3(freq: float) -> bytes:
    """The predecessor asset: a faded sine through lame.

    Fade-out + trailing silence keep the tone's end click-free (an un-faded
    sine ends mid-cycle and the encoder rings at the cut); -t drops the LAME
    Xing/Info frame the VS1003 would decode as a garbage frame.
    """
    with tempfile.TemporaryDirectory() as td:
        wav, mp3 = Path(td, "tone.wav"), Path(td, "tone.mp3")
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(synth_pcm(freq))
        subprocess.run(
            ["lame", "--quiet", "-b", str(BITRATE), "-t", str(wav), str(mp3)],
            check=True,
        )
        return mp3.read_bytes()


def emit_header(data: bytes, symbol: str, banner: list) -> str:
    lines = list(banner)
    lines.append("static const unsigned char %s[%d] = {" % (symbol, len(data)))
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join(str(b) for b in data[i : i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--format", choices=("midi", "mp3"), default="midi",
                   help="midi (default): a Standard MIDI File, no encoder needed. "
                        "mp3: the 2,160 B predecessor, via lame - see the "
                        "hardware gate in the module docstring")
    p.add_argument("-o", "--out", type=Path, default=None,
                   help="default inc/tone_<format>.h; a .mid/.mp3 suffix dumps "
                        "the raw file instead of the C header (probes)")
    p.add_argument("--freq", type=float, default=880.0,
                   help="mp3 only; the MIDI tone's pitch is NOTE above")
    args = p.parse_args()
    out = args.out or Path("inc/tone_%s.h" % args.format)

    if args.format == "midi":
        data = synth_smf(NOTE, TONE_MS)
        symbol, raw_suffix = "nab_tone_midi", ".mid"
        banner = [
            "/* Embedded %dms %s (880Hz) MIDI tone for nab.tone() - GENERATED, do not edit:"
            % (TONE_MS, NOTE),
            " *   task lua:firmware:gen:tone   (tools/tonegen.py: a Format-0 SMF, no encoder)",
            " * A Standard MIDI File, NOT MP3 and NOT raw PCM WAV: the VS1003B decodes",
            " * MIDI natively (#331 - %d B here where the MP3 predecessor was 2,160), and it"
            % len(data),
            " * does not decode PCM WAV at all. Byte-identical to what ../lib/audio/midi.lua",
            " * builds for audio.midi.note(\"%s\", %d), so the resident tone and a Lua-side"
            % (NOTE, TONE_MS),
            " * jingle cannot drift; test/host/tone_test.c pins every byte to the SMF spec. */",
        ]
    else:
        data = synth_mp3(args.freq)
        symbol, raw_suffix = "nab_tone_mp3", ".mp3"
        banner = [
            "/* Embedded %dms %dHz MP3 tone - GENERATED, do not edit:" % (TONE_MS, int(args.freq)),
            " *   task lua:firmware:gen:tone FORMAT=mp3   (faded sine -> lame -b%d -t)" % BITRATE,
            " * The pre-#331 nab.tone() asset, kept generatable while MIDI playback is",
            " * still ungated on the rig. Fade-out + trailing silence keep the tone's end",
            " * click-free; -t drops the Xing/Info frame the VS1003 decodes as garbage. */",
        ]

    if out.suffix == raw_suffix:
        out.write_bytes(data)
    else:
        out.write_text(emit_header(data, symbol, banner))
    print("tonegen: %s (%d bytes %s)" % (out, len(data), args.format.upper()))


if __name__ == "__main__":
    main()
