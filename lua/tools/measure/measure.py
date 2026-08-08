#!/usr/bin/env python3
"""Emit the measured figures the lua track's documents cite - one implementation.

`lua/ARCHITECTURE.md`, `lua/firmware/README.md`, `lua/boot/README.md` and the
per-lib READMEs all quote numbers, and before this every one of them came from a
different throwaway invocation (parse `arm-none-eabi-size`, walk the map, count
lines, count functions in main.c). Re-deriving them by hand is what let them
drift apart - three different sizes were once documented for the same boot chunk.

Six figures, one command (#342):

  flash      total/used/free of the internal flash region (the ELF's stored,
             allocated sections against the map's memory configuration)
  objects    per-object and per-directory flash, from the map's input sections
             (-ffunction-sections means a `.text.<fn>` per function, so the
             per-function split falls out of the same parse)
  boot       the resident boot chunk baked into the image (gen/boot_lc.h)
  bytecode   per-module stripped .lc size of every lua lib (host luac, Docker)
  lines      lines per source area, vendored dirs called out
  functions  function definitions in src/main.c - the count that keeps moving

Human output by default, `--format json` for machines (#338's size gate is the
consumer). Only `bytecode` needs Docker; the rest is a pure read of the source
tree and of build products Docker already produced.

Refusing to report stale numbers is half the point: the map and the ELF come
from a build, so a map older than any source it was built from is an error, not
a footnote. An empty parse is an error too - a report of zeros looks like a
measurement and is not one.

Python stdlib only.
"""
import argparse
import glob
import json
import os
import re
import struct
import subprocess
import sys
import time

# Every section this tool can emit, in report order. `--only` selects a subset.
SECTIONS = ("flash", "objects", "boot", "bytecode", "lines", "functions")

# Source areas, in the shape the documents cite them. Each is a list of globs
# relative to firmware/; an area's files are the union. The rule is deliberately
# mechanical: an area is `src/<a>/*.c` plus the headers that declare it,
# `inc/<a>/*.h`. The loose headers directly in `inc/` belong to no single area
# (common.h is included by 41 files), so they get their own row rather than
# being folded into whichever area a hand-written script happened to sweep.
AREAS = (
    ("src/main.c", ("src/main.c",), False),
    ("src/hal/", ("src/hal/*.c", "inc/hal/*.h"), False),
    ("src/utils/", ("src/utils/*.c", "inc/utils/*.h"), False),
    ("src/libc/", ("src/libc/*.c", "inc/libc/*.h"), False),
    ("inc/", ("inc/*.h",), False),
    ("sys/", ("sys/asm/*.s", "sys/src/*.c", "sys/inc/*.h", "sys/*.ld"), False),
    ("examples/", ("examples/*.c", "examples/*.h"), False),
    ("src/usb/", ("src/usb/*.c", "inc/usb/*.h"), True),
    ("src/net/", ("src/net/*.c", "inc/net/*.h"), True),
    ("lua/", ("lua/*.c", "lua/*.h"), True),
)

# What the linker map is built from. A map older than any of these is stale, and
# a stale map is the failure mode this tool exists to make impossible: yesterday's
# flash total is indistinguishable from today's until someone acts on it.
MAP_SOURCES = ("src", "inc", "sys", "lua", "gen", "Makefile")


# --------------------------------------------------------------------------
# linker map
# --------------------------------------------------------------------------

# An output section, at column 0: `.text  0x08000000  0x187b8`, plus
# ` load address 0x...` when it is stored somewhere other than where it runs
# (.data, and - misleadingly - .bss; see elf_flash_sections).
RE_OUT = re.compile(
    r"^(?P<name>\.[\w.$-]+)\s+0x(?P<vma>[0-9a-f]+)\s+0x(?P<size>[0-9a-f]+)"
    r"(?:\s+load address 0x(?P<lma>[0-9a-f]+))?\s*$"
)
RE_OUT_NAME = re.compile(r"^(?P<name>\.[\w.$-]+)\s*$")
# The continuation ld emits when a section name is too long to share its line.
RE_OUT_REST = re.compile(
    r"^\s+0x(?P<vma>[0-9a-f]+)\s+0x(?P<size>[0-9a-f]+)"
    r"(?:\s+load address 0x(?P<lma>[0-9a-f]+))?\s*$"
)
# An input section contributed by one object, indented:
# `.text.foo  0x08000204  0x28  obj/src/main.o`, wrapping the same way.
RE_IN = re.compile(
    r"^\s+(?P<name>\.[\w.$-]+)\s+0x(?P<vma>[0-9a-f]+)\s+0x(?P<size>[0-9a-f]+)"
    r"\s+(?P<obj>\S.*?)\s*$"
)
RE_IN_NAME = re.compile(r"^\s+(?P<name>\.[\w.$-]+)\s*$")
RE_IN_REST = re.compile(
    r"^\s+0x(?P<vma>[0-9a-f]+)\s+0x(?P<size>[0-9a-f]+)\s+(?P<obj>\S.*?)\s*$"
)
RE_REGION = re.compile(
    r"^(?P<name>\S+)\s+0x(?P<origin>[0-9a-f]+)\s+0x(?P<length>[0-9a-f]+)"
)


def parse_map(path):
    """Parse a GNU ld map into (regions, output sections, input sections).

    Two things the shape of the file forces. **Wrapping**: ld puts the address
    and size on the next line whenever the section name is too long to share it,
    for output and input sections alike, so a line-at-a-time parse needs one
    slot of lookbehind. **Attribution**: an input section belongs to whichever
    output section last appeared at column 0 - which is also what keeps the
    `Discarded input sections` block out of the totals, since no output section
    is open while it is being read.
    """
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    regions = []
    in_memcfg = False
    for line in lines:
        if line.startswith("Memory Configuration"):
            in_memcfg = True
            continue
        if in_memcfg:
            if line.startswith("Linker script and memory map"):
                break
            m = RE_REGION.match(line)
            if m and m.group("name") != "*default*":
                regions.append({
                    "name": m.group("name"),
                    "origin": int(m.group("origin"), 16),
                    "length": int(m.group("length"), 16),
                })

    def out_section(name, m):
        return {"name": name, "vma": int(m.group("vma"), 16),
                "size": int(m.group("size"), 16),
                "lma": int(m.group("lma") or m.group("vma"), 16)}

    outs, ins = [], []
    cur = None          # the output section input lines are attributed to
    pending_out = None  # an output-section name whose addresses wrapped
    pending_in = None   # an input-section name whose addresses wrapped
    for line in lines:
        if not line.strip():
            pending_out = pending_in = None
            continue

        if not line[0].isspace():
            pending_in = None
            m = RE_OUT.match(line)
            if m:
                cur = out_section(m.group("name"), m)
                outs.append(cur)
                pending_out = None
            elif RE_OUT_NAME.match(line):
                pending_out = RE_OUT_NAME.match(line).group("name")
            else:
                cur, pending_out = None, None   # OUTPUT(), LOAD, prose headings
            continue

        if pending_out is not None:
            m = RE_OUT_REST.match(line)
            if m:
                cur = out_section(pending_out, m)
                outs.append(cur)
            else:
                cur = None
            pending_out = None
            continue

        if cur is None:
            continue

        if pending_in is not None:
            m = RE_IN_REST.match(line)
            name, pending_in = pending_in, None
            if m:
                ins.append({"section": cur["name"], "name": name,
                            "size": int(m.group("size"), 16),
                            "obj": m.group("obj")})
                continue
            # else: fall through - the line may open a new input section

        if line.lstrip().startswith("*"):
            continue        # `*(.text.*)` wildcards and `*fill*` padding
        m = RE_IN.match(line)
        if m:
            ins.append({"section": cur["name"], "name": m.group("name"),
                        "size": int(m.group("size"), 16), "obj": m.group("obj")})
            continue
        m = RE_IN_NAME.match(line)
        if m:
            pending_in = m.group("name")
    return regions, outs, ins


def flash_region(regions, outs):
    """The region the code was linked into: the one holding `.text`'s address."""
    text = next((s for s in outs if s["name"] == ".text" and s["size"]), None)
    if text is None:
        raise Measured.Error("map has no non-empty .text output section")
    for r in regions:
        if r["origin"] <= text["lma"] < r["origin"] + r["length"]:
            return r
    raise Measured.Error(".text lies outside every memory region in the map")


def objdir(obj):
    """Group key for an object file: its directory, or the archive it came from.

    Archive members read `/path/libc_nano.a(lib_a-ctype_.o)`; they are grouped
    under the archive because that is the granularity the flash budget cares
    about - one stray libc call re-links a whole member (see firmware/README.md).
    Linker-generated input ("linker stubs", the interworking veneers) has no
    path and keeps its own name rather than being filed under a directory.
    """
    m = re.match(r"^(?:.*/)?([^/]+\.a)\(", obj)
    if m:
        return m.group(1)
    d = os.path.dirname(obj)
    if not d:
        return obj
    return (d[4:] + "/") if d.startswith("obj/") else d + "/"


# --------------------------------------------------------------------------
# ELF
# --------------------------------------------------------------------------

SHF_ALLOC = 0x2
SHT_NOBITS = 8


def elf_flash_sections(path):
    """Names and sizes of the sections that actually occupy flash.

    What `arm-none-eabi-size` calls text+data: allocated and stored. The
    distinction cannot be made from the map alone - ld prints a `load address`
    for `.bss` too, so trusting it would bill 5 KB of zero-init RAM to the flash
    budget. `SHT_NOBITS` is the bit that says "occupies no file space".
    """
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise Measured.Error(f"{os.path.basename(path)} is not a 32-bit "
                             "little-endian ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    if not shoff or not shnum:
        raise Measured.Error("ELF carries no section headers")
    def sh(i):
        name, typ, flags, addr, off, size = struct.unpack_from(
            "<IIIIII", data, shoff + i * shentsize)
        return name, typ, flags, addr, off, size
    _, _, _, _, stroff, _ = sh(shstrndx)
    out = []
    for i in range(shnum):
        name, typ, flags, _, _, size = sh(i)
        if not (flags & SHF_ALLOC) or typ == SHT_NOBITS or not size:
            continue
        end = data.index(b"\0", stroff + name)
        out.append({"name": data[stroff + name:end].decode(), "bytes": size})
    if not out:
        raise Measured.Error("ELF has no allocated, stored sections")
    return out


# --------------------------------------------------------------------------
# the figures
# --------------------------------------------------------------------------

class Measured:
    """Collects the figures. Each `do_*` fills one key of `self.data`."""

    class Error(Exception):
        pass

    def __init__(self, root, luac_image):
        self.root = root
        self.fw = os.path.join(root, "firmware")
        self.luac_image = luac_image
        self.data = {}
        self._map = None

    # -- helpers ----------------------------------------------------------

    def path(self, *parts):
        return os.path.join(self.fw, *parts)

    def need(self, rel, how="task lua:firmware:build"):
        p = self.path(rel)
        if not os.path.exists(p):
            raise Measured.Error(f"{rel} is missing - run `{how}` first")
        return p

    def check_fresh(self, artifact_rel, source_rels, how="task lua:firmware:build"):
        """Fail if `artifact` predates any of `sources` (a stale build product)."""
        art = self.need(artifact_rel, how)
        art_mtime = os.path.getmtime(art)
        newest, newest_mtime = None, 0.0
        for rel in source_rels:
            p = self.path(rel) if not os.path.isabs(rel) else rel
            if os.path.isfile(p):
                cands = [p]
            else:
                cands = [os.path.join(dp, f)
                         for dp, _, fs in os.walk(p) for f in fs]
            for c in cands:
                try:
                    mt = os.path.getmtime(c)
                except OSError:
                    continue
                if mt > newest_mtime:
                    newest, newest_mtime = c, mt
        if newest is not None and newest_mtime > art_mtime:
            rel = os.path.relpath(newest, self.fw)
            raise Measured.Error(
                f"{artifact_rel} is older than {rel} - the figures would be "
                f"yesterday's. Run `{how}` first.")
        return art

    def load_build(self):
        """The build products the flash figures come from, freshness-checked."""
        if self._map is None:
            map_path = self.check_fresh("obj/firmware.map", MAP_SOURCES)
            elf_path = self.check_fresh("bin/firmware.elf", MAP_SOURCES)
            regions, outs, ins = parse_map(map_path)
            region = flash_region(regions, outs)
            self._map = (map_path, region, elf_flash_sections(elf_path), ins)
        return self._map

    # -- sections ---------------------------------------------------------

    def do_flash(self):
        map_path, region, secs, _ = self.load_build()
        # `elf_flash_sections` already refuses an empty parse, so `used` is
        # positive by construction here - the "must not report zeros" guard for
        # this section lives there and in `flash_region`.
        used = sum(s["bytes"] for s in secs)
        self.data["flash"] = {
            "region": region["name"],
            "total_bytes": region["length"],
            "used_bytes": used,
            "free_bytes": region["length"] - used,
            "sections": secs,
            "elf": "firmware/bin/firmware.elf",
            "map": os.path.relpath(map_path, self.root),
        }

    def do_objects(self):
        map_path, _, secs, ins = self.load_build()
        flash_sections = {s["name"] for s in secs}
        per_obj, per_dir, per_fn = {}, {}, {}
        for i in ins:
            if i["section"] not in flash_sections or not i["size"]:
                continue
            per_obj[i["obj"]] = per_obj.get(i["obj"], 0) + i["size"]
            d = objdir(i["obj"])
            per_dir[d] = per_dir.get(d, 0) + i["size"]
            # -ffunction-sections: one `.text.<fn>` per function.
            if i["name"].startswith(".text."):
                key = f"{i['obj']}:{i['name'][6:]}"
                per_fn[key] = per_fn.get(key, 0) + i["size"]
        if not per_obj:
            raise Measured.Error(
                f"{os.path.relpath(map_path, self.fw)} yielded no per-object "
                "sizes - the input-section parse found nothing")
        counts = {}
        for obj in per_obj:
            counts[objdir(obj)] = counts.get(objdir(obj), 0) + 1
        self.data["objects"] = {
            "by_dir": [{"dir": d, "bytes": b, "objects": counts[d]}
                       for d, b in sorted(per_dir.items(), key=lambda kv: -kv[1])],
            "by_object": [{"object": o, "bytes": b}
                          for o, b in sorted(per_obj.items(), key=lambda kv: -kv[1])],
            "by_function": [{"function": f, "bytes": b}
                            for f, b in sorted(per_fn.items(), key=lambda kv: -kv[1])],
            # Attributed, not linked: mergeable string sections (.rodata.str*)
            # are billed to every object that contributed one but stored once, so
            # this runs a few hundred bytes over the ELF's flash total. Alignment
            # fill pushes the other way. Use `flash` for the budget.
            "attributed_bytes": sum(per_obj.values()),
        }

    def do_boot(self):
        # gen/boot_lc.h is generated from ../boot/boot.lua by the same build.
        path = self.check_fresh("gen/boot_lc.h",
                                [os.path.join(self.root, "boot", "boot.lua")])
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if "{" not in text or "}" not in text:
            raise Measured.Error("gen/boot_lc.h holds no C array")
        body = text[text.index("{") + 1:text.rindex("}")]
        n = len(re.findall(r"0x[0-9a-fA-F]{2}", body))
        if n <= 0:
            raise Measured.Error("gen/boot_lc.h holds no bytes")
        self.data["boot"] = {
            "source": "boot/boot.lua",
            "header": "firmware/gen/boot_lc.h",
            "bytes": n,
        }

    def do_bytecode(self):
        """Per-module stripped device bytecode - the `lua:lib:size` figures.

        Compiled through the host luac image (same vendored tree + LUA_32BITS
        luaconf.h as the device), source on stdin and chunk on stdout, so no
        host paths enter the container.
        """
        libroot = os.path.join(self.root, "lib")
        if not os.path.isdir(libroot):
            raise Measured.Error(f"no lib/ directory under {self.root}")
        mods, libs = [], {}
        for lib in sorted(os.listdir(libroot)):
            d = os.path.join(libroot, lib)
            if not os.path.isdir(d):
                continue
            for name in sorted(os.listdir(d)):
                if not name.endswith(".lua"):
                    continue
                with open(os.path.join(d, name), "rb") as f:
                    src = f.read()
                try:
                    proc = subprocess.run(
                        ["docker", "run", "--rm", "-i", self.luac_image,
                         "-s", "-o", "/dev/stdout", "-"],
                        input=src, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                except FileNotFoundError:
                    raise Measured.Error("docker not found on PATH (needed for "
                                         "the bytecode figures)")
                if proc.returncode != 0:
                    raise Measured.Error(
                        f"luac failed on lib/{lib}/{name}:\n"
                        + proc.stderr.decode("utf-8", "replace"))
                n = len(proc.stdout)
                if n <= 0:
                    raise Measured.Error(f"luac emitted 0 B for lib/{lib}/{name}")
                mods.append({"lib": lib, "module": name[:-4], "bytes": n})
                libs[lib] = libs.get(lib, 0) + n
        if not mods:
            raise Measured.Error(f"no .lua modules found under {libroot}")
        self.data["bytecode"] = {
            "modules": mods,
            "by_lib": [{"lib": k, "bytes": v} for k, v in sorted(libs.items())],
            "module_count": len(mods),
            "total_bytes": sum(m["bytes"] for m in mods),
        }

    def do_lines(self):
        areas = []
        for name, globs, vendored in AREAS:
            files = []
            for g in globs:
                files.extend(sorted(glob.glob(self.path(g))))
            if not files:
                raise Measured.Error(f"area {name} matched no files - the globs "
                                     "in AREAS have gone stale")
            total = 0
            for p in files:
                with open(p, "rb") as f:
                    total += f.read().count(b"\n")
            areas.append({"area": name, "files": len(files), "lines": total,
                          "vendored": vendored})
        self.data["lines"] = {
            "areas": areas,
            "total_lines": sum(a["lines"] for a in areas),
            "own_lines": sum(a["lines"] for a in areas if not a["vendored"]),
        }

    def do_functions(self):
        path = self.path("src/main.c")
        fns = c_functions(path)
        if not fns:
            raise Measured.Error("found 0 functions in src/main.c")
        with open(path, "rb") as f:
            lines = f.read().count(b"\n")
        self.data["functions"] = {
            "file": "firmware/src/main.c",
            "lines": lines,
            "count": len(fns),
            "names": fns,
        }


# A function *definition* in this tree's style: the return type and name start
# at column 0, the parameter list closes on the same line, and the body's brace
# follows (same line or next). Declarations end in `;` and are excluded, which
# is what separates this from "count the parens".
RE_FN = re.compile(r"^[A-Za-z_][A-Za-z0-9_\s*]*?[A-Za-z0-9_*]\s*"
                   r"\((?P<args>[^;{}]*)\)\s*(?P<tail>\{.*)?$")
RE_FN_NAME = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def c_functions(path):
    """Names of the functions defined in a C file, in source order."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    out = []
    for i, line in enumerate(lines):
        if not line or line[0].isspace() or line.startswith(("#", "/", "*", "}")):
            continue
        m = RE_FN.match(line)
        if not m:
            continue
        if not m.group("tail"):
            nxt = next((l for l in lines[i + 1:] if l.strip()), "")
            if not nxt.startswith("{"):
                continue
        names = RE_FN_NAME.findall(line[:line.index("(")] + "(")
        if names and names[-1] not in ("if", "for", "while", "switch", "return"):
            out.append(names[-1])
    return out


# --------------------------------------------------------------------------
# reporting
# --------------------------------------------------------------------------

def n(x):
    return f"{x:,}"


def report(data, out):
    w = out.write
    if "flash" in data:
        f = data["flash"]
        w(f"FLASH  ({f['region']}, {n(f['total_bytes'])} B)\n")
        pct = 100.0 * f["used_bytes"] / f["total_bytes"]
        w(f"  {n(f['used_bytes']):>12} B used   {pct:.1f}%\n")
        w(f"  {n(f['free_bytes']):>12} B free\n")
        w("  " + "  ".join(f"{s['name']} {n(s['bytes'])} B"
                           for s in f["sections"]) + "\n\n")
    if "objects" in data:
        o = data["objects"]
        w("FLASH BY DIRECTORY\n")
        for d in o["by_dir"]:
            w(f"  {d['dir']:<24} {n(d['bytes']):>9} B  {d['objects']:>3} obj\n")
        w(f"  {'TOTAL (attributed)':<24} {n(o['attributed_bytes']):>9} B"
          "   merged strings are billed per contributor, so this\n"
          f"  {'':<24} {'':>9}     runs over the linked total - `flash` is the budget\n\n")
        w("LARGEST OBJECTS\n")
        for e in o["by_object"][:10]:
            w(f"  {e['object']:<40} {n(e['bytes']):>8} B\n")
        w("\nLARGEST FUNCTIONS\n")
        for e in o["by_function"][:10]:
            w(f"  {e['function']:<40} {n(e['bytes']):>8} B\n")
        w("\n")
    if "boot" in data:
        b = data["boot"]
        w("RESIDENT BOOT CHUNK\n")
        w(f"  {b['source']} -> {b['header']}   {n(b['bytes'])} B of flash\n\n")
    if "bytecode" in data:
        b = data["bytecode"]
        subtotal = {l["lib"]: l["bytes"] for l in b["by_lib"]}
        w("LUA BYTECODE  (stripped .lc, host luac)\n")
        cur = None
        for m in b["modules"]:
            if cur is not None and m["lib"] != cur:
                w(f"  {'':<24} {n(subtotal[cur]):>8} B  ({cur} subtotal)\n")
            cur = m["lib"]
            w(f"  {m['lib'] + '/' + m['module']:<24} {n(m['bytes']):>8} B\n")
        if cur is not None:
            w(f"  {'':<24} {n(subtotal[cur]):>8} B  ({cur} subtotal)\n")
        w(f"  {'TOTAL':<24} {n(b['total_bytes']):>8} B"
          f"  ({b['module_count']} modules, RAM)\n\n")
    if "lines" in data:
        l = data["lines"]
        w("LINES BY AREA\n")
        for a in l["areas"]:
            tag = "  (vendored)" if a["vendored"] else ""
            plural = "file " if a["files"] == 1 else "files"
            w(f"  {a['area']:<16} {a['files']:>3} {plural}  {n(a['lines']):>8} ln{tag}\n")
        w(f"  {'TOTAL':<16} {'':>3}         {n(l['total_lines']):>8} ln"
          f"   ({n(l['own_lines'])} ln ours)\n\n")
    if "functions" in data:
        fn = data["functions"]
        w(f"{fn['file']}\n")
        w(f"  {n(fn['lines'])} lines · {fn['count']} functions\n\n")


def find_root(start):
    """The lua layer root: the dir holding firmware/ + lib/ + boot/."""
    d = os.path.abspath(start)
    while True:
        if all(os.path.isdir(os.path.join(d, x))
               for x in ("firmware", "lib", "boot")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            sys.exit("measure: could not find the lua layer root (pass --root)")
        d = parent


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=None,
                    help="lua layer root (default: found by walking up)")
    ap.add_argument("--format", choices=("text", "json"), default="text")
    ap.add_argument("--only", default=None,
                    help="comma-separated subset of: " + ", ".join(SECTIONS))
    ap.add_argument("--luac-image", default="nabaztag-sdk-luac",
                    help="host luac image for the bytecode figures")
    args = ap.parse_args()

    wanted = SECTIONS
    if args.only:
        wanted = tuple(s.strip() for s in args.only.split(",") if s.strip())
        bad = [s for s in wanted if s not in SECTIONS]
        if bad:
            sys.exit(f"measure: unknown section(s): {', '.join(bad)}\n"
                     f"         known: {', '.join(SECTIONS)}")

    root = args.root or find_root(os.path.dirname(os.path.abspath(__file__)))
    m = Measured(root, args.luac_image)
    try:
        for s in wanted:
            getattr(m, "do_" + s)()
    except Measured.Error as e:
        sys.exit(f"measure: {e}")

    if not m.data:
        sys.exit("measure: nothing measured")

    if args.format == "json":
        m.data["measured_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        json.dump(m.data, sys.stdout, indent=2, sort_keys=False)
        sys.stdout.write("\n")
    else:
        report(m.data, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
