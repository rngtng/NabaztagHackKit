#!/usr/bin/env python3
"""MTL preprocessor backed by pcpp — #ifdef/#include/#define/#pragma once pipeline."""
import argparse
import io
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

try:
    import pcpp
except ImportError:
    sys.exit("pcpp not installed — run: pip install pcpp")

_FUN   = re.compile(r"^fun ([a-zA-Z0-9_-]+)")
_PROTO = re.compile(r"^proto ([a-zA-Z0-9_-]+)")


def _date():
    dt = datetime.now(_TZ) if _TZ else datetime.utcnow()
    return dt.isoformat(timespec="seconds")


class _MTLPreprocessor(pcpp.Preprocessor):
    """pcpp subclass for MTL: comment passthrough, HTML/CSS quote-escaping on include."""

    def __init__(self):
        super().__init__()
        self.line_directive = None     # suppress #line markers (MTL compiler rejects them)
        self.passthru_comments = True  # keep // and /* */ comments

    def on_error(self, file, line, msg):
        sys.exit(f"{file}:{line}: {msg}")

    def on_file_open(self, is_system_include, path):
        """Handle extension-less includes, directory collisions, and HTML/CSS escaping."""
        p = Path(path)
        # Extension-less include (e.g. #include "forth" where lib/forth/ also exists):
        # use is_file() to skip directories, then retry with .mtl suffix.
        if not p.is_file() and not p.suffix:
            mtl = p.with_suffix('.mtl')
            if mtl.is_file():
                return super().on_file_open(is_system_include, str(mtl))
        # Escape double-quotes in HTML/CSS so they embed cleanly in MTL strings.
        if p.suffix.lower() in ('.html', '.css'):
            with open(path, 'r') as fh:
                return io.StringIO(fh.read().replace('"', '\\"'))
        result = super().on_file_open(is_system_include, path)
        # treat every file as #pragma once — no need for the directive in sources
        self.include_once.add(os.path.realpath(path))
        return result


def preprocess(source, defines, include_dirs):
    """Return preprocessed output lines."""
    pp = _MTLPreprocessor()

    for d in defines:
        pp.define(f"{d} 1")
    pp.define(f"__DATE__ {_date()!r}")

    pp.add_path(".")  # always search from repo root so "lib/foo.mtl" resolves everywhere
    for d in include_dirs:
        pp.add_path(str(d))

    pp.parse(Path(source).read_text(), str(source))

    out = io.StringIO()
    pp.write(out)
    return out.getvalue().splitlines(keepends=True)


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
    """Map a processed-file line number back to a source location.

    Note: the pcpp backend suppresses line directives for MTL compiler compatibility,
    so file-level source tracking is unavailable. The line number in the merged file
    is returned as-is.
    """
    return f"{processed_path}, line {target}"


def _self_test():
    with tempfile.TemporaryDirectory() as d:
        Path(d, "inc.mtl").write_text("#pragma once\nfun helper=\n  0\n;;\n")
        Path(d, "page.html").write_text('<p>"hello"</p>\n')
        # Directory collision: lib/forth/ dir alongside lib/forth.mtl
        (Path(d) / "forth").mkdir()
        Path(d, "forth.mtl").write_text("#pragma once\nfun forth_word= 0;;\n")
        Path(d, "main.mtl").write_text(textwrap.dedent("""\
            #define FEATURE
            #ifdef FEATURE
            proto main 0;;
            #endif
            #ifdef MISSING
            proto skip 0;;
            #endif
            #include "inc.mtl"
            #include "inc.mtl"
            proto helper 0;;
            #include "forth"
            fun main=
              0
            ;;
            var page="
            #include "page.html"
            ";;
        """))
        old = os.getcwd()
        os.chdir(d)
        try:
            lines = preprocess(Path(d, "main.mtl"), [], [d])
            lines = remove_extra_protos(lines)
            text = "".join(lines)
            assert "proto main" in text,         "ifdef define failed"
            assert "proto skip" not in text,     "ifdef missing should be excluded"
            assert "fun helper" in text,         "include failed"
            assert "//proto helper" in text,     "proto dedup failed"
            assert text.count("fun helper") == 1, "pragma once dedup failed"
            assert '\\"hello\\"' in text,         "html quote escaping failed"
            assert "fun forth_word" in text,      "extension-less include failed"
        finally:
            os.chdir(old)
    print("self-test OK")


def main():
    ap = argparse.ArgumentParser(description="MTL preprocessor (pcpp backend)")
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

    lines = preprocess(Path(args.source), args.defines, args.include_dirs)
    lines = remove_extra_protos(lines)
    sys.stdout.write("".join(lines))


if __name__ == "__main__":
    main()
