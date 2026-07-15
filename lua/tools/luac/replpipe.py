#!/usr/bin/env python3
"""Convert Lua REPL input into the lua-firmware #LC bytecode-frame stream (#133).

The parser-less wifi image (M11, #119) only loads luac bytecode, so the REPL is
fed compiled chunks instead of source. Raw bytecode can't ride the line-oriented
UART console (chunks contain '\\n'/NUL), so each chunk is framed as:

    #LC:<len>\\n            header line, len = chunk size in bytes (decimal)
    <2*len hex chars>      the chunk, wrapped at 64 cols (device skips whitespace)

Two input modes, chosen by extension (override with --lua/--lc):

  *.lua : one frame per source line, compiled with the same expression-first
          fallback the device's load_line uses ("return <line>", then the line
          verbatim) - so a bare expression echoes its value and the prompt count
          matches feeding the source directly. A line that fails both ways is
          reported on stderr and emitted as nothing (mirrors nothing the device
          could round-trip; keep round-trip scripts error-free).

  *.lc  : the file is already a compiled chunk - shipped as a single frame.

Compilation goes through the host luac image (tools/luac), built from the same
vendored lua/ tree + luaconf.h as the firmware, so the bytecode header sizes
match what the rabbit's lundump.c accepts. Python stdlib only.
"""
import argparse
import subprocess
import sys


def luac_compile(src: bytes, image: str) -> tuple[bytes | None, str]:
    """Compile `src` to stripped bytecode via the luac Docker image.

    Returns (chunk, stderr). chunk is None on a compile error. Source rides
    stdin and the chunk rides stdout (`-o /dev/stdout -`), so no bind mounts and
    no host paths leak into the container.
    """
    try:
        proc = subprocess.run(
            ["docker", "run", "--rm", "-i", image,
             "-s", "-o", "/dev/stdout", "-"],
            input=src, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    except FileNotFoundError:
        sys.exit("replpipe: docker not found on PATH")
    if proc.returncode != 0:
        return None, proc.stderr.decode("utf-8", "replace")
    return proc.stdout, proc.stderr.decode("utf-8", "replace")


def emit_frame(out, chunk: bytes) -> None:
    """Write one #LC frame: header line + hex payload wrapped at 64 columns."""
    out.write(f"#LC:{len(chunk)}\n")
    hexs = chunk.hex()
    for i in range(0, len(hexs), 64):
        out.write(hexs[i:i + 64] + "\n")


def frame_lua_source(path: str, image: str, out) -> int:
    """Emit one frame per source line. Returns the number of compile errors."""
    with open(path, "rb") as f:
        lines = f.readlines()  # keeps the trailing '\n', as the device's sh_gets does
    errors = 0
    for line in lines:
        # Mirror the device load_line(): try "return <line>" (so a bare
        # expression evaluates and echoes), then the line verbatim.
        chunk, _ = luac_compile(b"return " + line, image)
        if chunk is None:
            chunk, err = luac_compile(line, image)
        if chunk is None:
            sys.stderr.write(err)
            errors += 1
            continue
        emit_frame(out, chunk)
    return errors


def frame_lc_file(path: str, out) -> None:
    """Ship an already-compiled .lc chunk as a single frame."""
    with open(path, "rb") as f:
        emit_frame(out, f.read())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="REPL input: a .lua source file or a .lc chunk")
    ap.add_argument("-o", "--output", help="write frames here (default: stdout)")
    ap.add_argument("--image", default="nabaztag-sdk-luac",
                    help="luac Docker image (default: nabaztag-sdk-luac)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--lua", action="store_true", help="force .lua (per-line) mode")
    mode.add_argument("--lc", action="store_true", help="force .lc (single-frame) mode")
    args = ap.parse_args()

    as_lc = args.lc or (not args.lua and args.input.endswith(".lc"))

    out = open(args.output, "w") if args.output else sys.stdout
    try:
        if as_lc:
            frame_lc_file(args.input, out)
            return 0
        return 1 if frame_lua_source(args.input, args.image, out) else 0
    finally:
        if args.output:
            out.close()


if __name__ == "__main__":
    sys.exit(main())
