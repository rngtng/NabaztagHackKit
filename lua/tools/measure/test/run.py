#!/usr/bin/env python3
"""Unit tests for the measure parsers (#342).

Every figure `task lua:measure` prints comes from one of four parses - the
linker map, the ELF section table, the C function scanner, and the
freshness/non-vacuity guards - so those are what is tested here, against inputs
whose answers are known by construction rather than by re-running the tool.

The fixture map is small but carries every shape the real one does: a wrapped
output section, a wrapped input section, `*fill*` padding, an archive member, a
`Discarded input sections` block, and a `.bss` that advertises a load address in
flash while occupying none of it. Each scenario asserts specific expected
numbers - "the two runs agreed" would pass on two empty parses.

Stdlib only, no Docker, milliseconds: this runs inside `task lua:verify`.
"""
import os
import struct
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import measure  # noqa: E402


SAMPLE_MAP = """\
Archive member included to satisfy reference by file (symbol)

/usr/lib/gcc/arm-none-eabi/12.2.1/../../../arm-none-eabi/lib/libc_nano.a(lib_a-ctype_.o)
                              obj/lua/lbaselib.o (_ctype_)

Discarded input sections

 .text.dropped  0x00000000       0x40 obj/src/hal/led.o
 .text.also_dropped
                0x00000000       0x80 obj/src/hal/led.o

Memory Configuration

Name             Origin             Length             Attributes
IntROM           0x08000000         0x0001f000         xr
IntRAM           0x10000000         0x00004000         rw
*default*        0x00000000         0xffffffff

Linker script and memory map

LOAD obj/src/main.o
                0x08000000                _stext = .

.text           0x08000000      0x100
 *(.intvec)
 .intvec        0x08000000       0x10 obj/sys/asm/init.o
                0x08000000                Reset_Handler
 *(.text.*)
 .text.short    0x08000010       0x20 obj/src/main.o
 .text.a_function_with_a_very_long_name
                0x08000030       0x30 obj/src/main.o
 *fill*         0x08000060        0x2
 .text.helper   0x08000062       0x40 obj/src/hal/led.o
 .text          0x080000a2       0x10 /usr/lib/gcc/arm-none-eabi/12.2.1/../../../arm-none-eabi/lib/libc_nano.a(lib_a-ctype_.o)
 .text.__stub   0x080000b2       0x10 linker stubs

.rodata         0x08000100       0x40
 .rodata.tab    0x08000100       0x40 obj/src/main.o

.data           0x10000000       0x20 load address 0x08000140
 .data.x        0x10000000       0x20 obj/src/main.o

.bss            0x10000020       0x80 load address 0x08000160
 .bss.buf       0x10000020       0x80 obj/src/hal/led.o

.debug_line_str
                0x00000000       0x50
 .debug_line_str
                0x00000000       0x50 obj/src/main.o
"""

SAMPLE_C = """\
/* A header comment mentioning main(void) in prose. */
#include <stdio.h>

static int a_declaration(int x);        /* not a definition */

struct s {
    int (*fnptr)(void);                 /* not a definition either */
};

static void first(void)
{
    if (a_declaration(1)) {
        while (0) { }
    }
}

int second(const char *s, int n)
{
    return n;
}

static unsigned int *third(void) { return 0; }

int main(void)
{
    return 0;
}
"""

# name -> (type, flags, size); SHT_PROGBITS = 1, SHT_NOBITS = 8, SHF_ALLOC = 2
SAMPLE_ELF_SECTIONS = [
    ("", 0, 0, 0),
    (".text", 1, 0x6, 0x100),
    (".rodata", 1, 0x2, 0x40),
    (".data", 1, 0x3, 0x20),
    (".bss", 8, 0x3, 0x80),          # allocated but stored nowhere
    (".comment", 1, 0x0, 0x26),      # stored but not allocated
    (".shstrtab", 3, 0x0, 0),
]


def write_elf(path):
    """Emit a minimal ELF32-LE whose section table is SAMPLE_ELF_SECTIONS."""
    names = [s[0] for s in SAMPLE_ELF_SECTIONS]
    strtab, offsets = b"", {}
    for nm in names:
        offsets[nm] = len(strtab)
        strtab += nm.encode() + b"\0"
    shoff = 52 + len(strtab)
    hdr = bytearray(52)
    hdr[0:4] = b"\x7fELF"
    hdr[4], hdr[5], hdr[6] = 1, 1, 1          # 32-bit, little-endian, v1
    struct.pack_into("<HHI", hdr, 16, 2, 40, 1)   # e_type EXEC, e_machine ARM
    struct.pack_into("<I", hdr, 0x20, shoff)      # e_shoff
    struct.pack_into("<HHH", hdr, 0x2E, 40, len(SAMPLE_ELF_SECTIONS),
                     len(SAMPLE_ELF_SECTIONS) - 1)  # shentsize, shnum, shstrndx
    body = b""
    for nm, typ, flags, size in SAMPLE_ELF_SECTIONS:
        off = 52 if nm == ".shstrtab" else 0
        sz = len(strtab) if nm == ".shstrtab" else size
        body += struct.pack("<IIIIIIIIII", offsets[nm], typ, flags, 0, off, sz,
                            0, 0, 1, 0)
    with open(path, "wb") as f:
        f.write(bytes(hdr) + strtab + body)


# --------------------------------------------------------------------------

FAILED = []


def check(scenario, got, want):
    ok = got == want
    print(f"  {'ok  ' if ok else 'FAIL'} {scenario}: {got!r}"
          + ("" if ok else f"  (want {want!r})"))
    if not ok:
        FAILED.append(scenario)


def expect_error(scenario, fn, needle):
    try:
        fn()
    except measure.Measured.Error as e:
        ok = needle in str(e)
        print(f"  {'ok  ' if ok else 'FAIL'} {scenario}: {e}"
              + ("" if ok else f"  (want a message containing {needle!r})"))
        if not ok:
            FAILED.append(scenario)
        return
    print(f"  FAIL {scenario}: no error raised")
    FAILED.append(scenario)


def test_map(tmp):
    print("== linker map ==")
    path = os.path.join(tmp, "sample.map")
    with open(path, "w") as f:
        f.write(SAMPLE_MAP)
    regions, outs, ins = measure.parse_map(path)

    check("regions", [(r["name"], r["length"]) for r in regions],
          [("IntROM", 0x1f000), ("IntRAM", 0x4000)])
    check("flash region", measure.flash_region(regions, outs)["name"], "IntROM")
    # .debug_line_str is the wrapped output section: dropping it would mean the
    # parse silently loses whatever follows a long name.
    check("output sections", [s["name"] for s in outs],
          [".text", ".rodata", ".data", ".bss", ".debug_line_str"])
    check(".data load address", [s["lma"] for s in outs if s["name"] == ".data"],
          [0x08000140])

    flash = {".text", ".rodata", ".data"}
    per_obj = {}
    for i in ins:
        if i["section"] in flash:
            per_obj[i["obj"]] = per_obj.get(i["obj"], 0) + i["size"]
    # main.o: .text.short 0x20 + the wrapped .text.a_function… 0x30
    #       + .rodata.tab 0x40 + .data.x 0x20 = 0xb0
    check("main.o bytes", per_obj.get("obj/src/main.o"), 0xb0)
    # led.o contributes one live .text.helper (0x40). Its two discarded sections
    # (0x40 + 0x80) and its 0x80 of .bss must not be billed to flash.
    check("led.o bytes", per_obj.get("obj/src/hal/led.o"), 0x40)
    check("no .bss in flash", [i for i in ins
                               if i["section"] == ".bss" and i["obj"] in per_obj
                               and i["section"] in flash], [])
    check("archive member grouped",
          measure.objdir("/usr/lib/gcc/x/libc_nano.a(lib_a-ctype_.o)"),
          "libc_nano.a")
    check("linker stubs grouped", measure.objdir("linker stubs"), "linker stubs")
    check("object dir", measure.objdir("obj/src/hal/led.o"), "src/hal/")
    fns = {i["name"][6:]: i["size"] for i in ins
           if i["name"].startswith(".text.") and i["section"] == ".text"}
    check("per-function", fns, {"short": 0x20,
                                "a_function_with_a_very_long_name": 0x30,
                                "helper": 0x40, "__stub": 0x10})


def test_elf(tmp):
    print("== ELF section table ==")
    path = os.path.join(tmp, "sample.elf")
    write_elf(path)
    secs = measure.elf_flash_sections(path)
    # .bss is allocated but SHT_NOBITS, .comment is stored but not allocated:
    # counting either would misreport the flash budget (.bss alone is 5 KB on
    # the real image).
    check("stored+allocated only", [(s["name"], s["bytes"]) for s in secs],
          [(".text", 0x100), (".rodata", 0x40), (".data", 0x20)])
    check("flash used", sum(s["bytes"] for s in secs), 0x160)

    notelf = os.path.join(tmp, "not.elf")
    with open(notelf, "wb") as f:
        f.write(b"not an elf at all")
    expect_error("rejects a non-ELF", lambda: measure.elf_flash_sections(notelf),
                 "not a 32-bit")


def test_functions(tmp):
    print("== C function scanner ==")
    path = os.path.join(tmp, "sample.c")
    with open(path, "w") as f:
        f.write(SAMPLE_C)
    # A prototype, a function-pointer member and the `if`/`while` inside a body
    # all look like "a name followed by parens"; only definitions count.
    check("definitions", measure.c_functions(path),
          ["first", "second", "third", "main"])


def test_guards(tmp):
    print("== freshness + non-vacuity guards ==")
    root = os.path.join(tmp, "lua")
    fw = os.path.join(root, "firmware")
    for d in ("obj", "bin", "src", "gen"):
        os.makedirs(os.path.join(fw, d), exist_ok=True)
    os.makedirs(os.path.join(root, "boot"), exist_ok=True)
    m = measure.Measured(root, "nabaztag-sdk-luac")

    expect_error("missing map", m.do_flash, "run `task lua:firmware:build`")

    with open(os.path.join(fw, "obj", "firmware.map"), "w") as f:
        f.write(SAMPLE_MAP)
    write_elf(os.path.join(fw, "bin", "firmware.elf"))
    src = os.path.join(fw, "src", "main.c")
    with open(src, "w") as f:
        f.write(SAMPLE_C)
    # A source edited after the link is exactly the case that used to print
    # yesterday's numbers as if they were today's.
    os.utime(src, (time.time() + 60, time.time() + 60))
    expect_error("stale map", m.do_flash, "older than src/main.c")

    os.utime(src, (time.time() - 60, time.time() - 60))
    m._map = None
    m.do_flash()
    check("fresh map measures", m.data["flash"]["used_bytes"], 0x160)
    check("free is region minus used", m.data["flash"]["free_bytes"],
          0x1f000 - 0x160)

    # An empty parse must fail rather than report a tidy row of zeros.
    empty = measure.Measured(root, "x")
    with open(os.path.join(fw, "obj", "firmware.map"), "w") as f:
        f.write("Memory Configuration\n\nName Origin Length\n")
    expect_error("empty map", empty.do_flash, "no non-empty .text")

    nofns = measure.Measured(root, "x")
    with open(src, "w") as f:
        f.write("/* comments only */\n")
    expect_error("no functions", nofns.do_functions, "0 functions")


def main():
    ran = 0
    with tempfile.TemporaryDirectory() as tmp:
        for fn in (test_map, test_elf, test_functions, test_guards):
            fn(tmp)
            ran += 1
    if ran == 0:
        print("measure test: no scenarios ran")
        return 1
    if FAILED:
        print(f"\n✗ {len(FAILED)} check(s) failed: {', '.join(FAILED)}")
        return 1
    print(f"\n✓ measure: {ran} scenarios, all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
