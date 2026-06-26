#!/usr/bin/env python3
"""MTL preprocessor: #ifdef/#include/proto-dedup pipeline."""
import argparse
import os
import re
import sys
import tempfile
import textwrap
from datetime import datetime
from pathlib import Path

try:
    from zoneinfo import ZoneInfo
    _TZ = ZoneInfo("Europe/Paris")
except Exception:
    _TZ = None

_IFDEF   = re.compile(r"^#ifdef ([a-zA-Z0-9_-]+)")
_IFNDEF  = re.compile(r"^#ifndef ([a-zA-Z0-9_-]+)")
_ELSE    = re.compile(r"^#else\b")
_ENDIF   = re.compile(r"^#endif\b")
_DEFINE  = re.compile(r"^#define ([a-zA-Z0-9_-]+)")
_INCLUDE = re.compile(r'^#include "?([a-zA-Z0-9._/\-]+)"?')
_FUN     = re.compile(r"^fun ([a-zA-Z0-9_-]+)")
_PROTO   = re.compile(r"^proto ([a-zA-Z0-9_-]+)")


def _date():
    dt = datetime.now(_TZ) if _TZ else datetime.utcnow()
    return dt.isoformat(timespec="seconds")


def _resolve(raw, include_dirs):
    p = Path(raw)
    for base in [Path(".")] + [Path(d) for d in include_dirs]:
        c = base / p
        if c.exists():
            return c
        if not c.suffix:
            mtl = c.with_suffix(".mtl")
            if mtl.exists():
                return mtl
    return None


def preprocess(source, defines, include_dirs):
    """Return list of output lines with includes expanded and ifdefs resolved."""
    included, out, date = set(), [], _date()

    def process(path, label):
        is_html = path.suffix.lower() in (".html", ".css")
        included.add(str(path))
        skip = [False]  # True = currently skipping; stack mirrors ifdef nesting

        for n, raw in enumerate(path.read_text().splitlines(keepends=True), 1):
            line = raw.replace("__DATE__", date)
            if is_html:
                line = line.replace('"', '\\"')

            if m := _IFDEF.match(line):
                skip.append(skip[-1] or (m.group(1) not in defines))
                continue
            if m := _IFNDEF.match(line):
                skip.append(skip[-1] or (m.group(1) in defines))
                continue
            if _ELSE.match(line):
                if len(skip) < 2:
                    sys.exit(f"{label}:{n}: #else without #ifdef")
                # flip current level but keep parent skip if it was already skipping
                skip[-1] = skip[-2] or (not skip[-1])
                continue
            if _ENDIF.match(line):
                if len(skip) < 2:
                    sys.exit(f"{label}:{n}: #endif without #ifdef")
                skip.pop()
                continue

            if skip[-1]:
                continue

            if m := _DEFINE.match(line):
                defines.add(m.group(1))
                continue

            if m := _INCLUDE.match(line):
                raw_inc = m.group(1)
                resolved = _resolve(raw_inc, include_dirs)
                if resolved is None:
                    sys.exit(f"{label}:{n}: cannot find include '{raw_inc}'")
                if str(resolved) in included:
                    out.append(f"// file {raw_inc} already included\n")
                    continue
                out.append(f"// file {raw_inc} //\n")
                process(resolved, raw_inc)
                out.append(f"// end of file {raw_inc} //\n")
                out.append(f"// back to file {label}, line {n}\n")
                continue

            out.append(line)

        if len(skip) != 1:
            sys.exit(f"{label}: unclosed #ifdef at end of file")

    process(source, str(source))
    return out


def remove_extra_protos(lines):
    """Comment out proto declarations that appear after the fun definition."""
    funs, out = set(), []
    for line in lines:
        if m := _FUN.match(line):
            funs.add(m.group(1))
        elif (m := _PROTO.match(line)) and m.group(1) in funs:
            line = "//" + line
        out.append(line)
    return out


def find_line(processed_path, target):
    """Map a line number in a processed file back to its original source location."""
    cur_file, cur_line, n = str(processed_path), 0, 0
    with open(processed_path) as f:
        for raw in f:
            n += 1
            if n == target:
                break
            if (m := re.match(r"^// file (.+?) //\s*$", raw)) and "already included" not in raw:
                cur_file, cur_line = m.group(1), 1
                continue
            if m := re.match(r"^// back to file (.+?), line (\d+)", raw):
                cur_file, cur_line = m.group(1), int(m.group(2))
                continue
            if re.match(r"^// end of file", raw):
                continue
            cur_line += 1
    return f"{cur_file}, line {cur_line}"


def _self_test():
    with tempfile.TemporaryDirectory() as d:
        Path(d, "inc.mtl").write_text("fun helper=\n  0\n;;\n")
        Path(d, "main.mtl").write_text(textwrap.dedent("""\
            #define FEATURE
            #ifdef FEATURE
            proto main 0;;
            #endif
            #ifdef MISSING
            proto skip 0;;
            #endif
            #include inc.mtl
            proto helper 0;;
            fun main=
              0
            ;;
        """))
        old = os.getcwd()
        os.chdir(d)
        try:
            lines = preprocess(Path(d, "main.mtl"), set(), [])
            lines = remove_extra_protos(lines)
            text = "".join(lines)
            assert "proto main" in text, "ifdef define failed"
            assert "proto skip" not in text, "ifdef missing should be excluded"
            assert "fun helper" in text, "include failed"
            assert "//proto helper" in text, "proto dedup failed"
        finally:
            os.chdir(old)
    print("self-test OK")


def main():
    ap = argparse.ArgumentParser(description="MTL preprocessor")
    ap.add_argument("-D", "--define", action="append", default=[], dest="defines", metavar="NAME")
    ap.add_argument("-I", action="append", default=[], dest="include_dirs", metavar="DIR")
    ap.add_argument("--test", action="store_true", help="run self-test and exit")
    ap.add_argument("--find-line", nargs=2, metavar=("FILE", "LINENUM"),
                    help="map line number in processed file back to source location")
    ap.add_argument("source", nargs="?")
    args = ap.parse_args()

    if args.find_line:
        file, linenum = args.find_line
        print(find_line(Path(file), int(linenum)))
        return

    if args.test:
        _self_test()
        return

    if not args.source:
        ap.error("source file required")

    lines = preprocess(Path(args.source), set(args.defines), args.include_dirs)
    lines = remove_extra_protos(lines)
    sys.stdout.write("".join(lines))


if __name__ == "__main__":
    main()
