# tools/measure — the figures the documents cite

Four documents in this track quote measured numbers — [`lua/ARCHITECTURE.md`](../../ARCHITECTURE.md),
[`firmware/README.md`](../../firmware/README.md), [`boot/README.md`](../../boot/README.md)
and the per-lib READMEs — and before this leaf every one of them came from its own
throwaway invocation: parse `arm-none-eabi-size`, walk `obj/firmware.map`, count lines
per directory, count functions in `main.c`. They were re-derived by hand four times
during the #322/#326 arc, and they drifted: three different sizes were once documented
for the same boot chunk. This is the one producer (#342).

```sh
task lua:measure                      # the human report
task lua:measure FORMAT=json          # the same figures, structured
task lua:measure ONLY=flash,lines     # a subset
task lua:measure:test                 # the parsers' unit tests (in lua:verify)
```

Reporting only — it is not a gate, and it is deliberately not in `lua:verify`.
Comparing what it prints against what the docs *say* is #338; this exists so that
gate has one number to compare against instead of four human-readable outputs.

## What it emits

| Section | Figure | Read from |
|---|---|---|
| `flash` | used / free / total, and the per-section split | `bin/firmware.elf` + the map's memory config |
| `objects` | flash per object, per directory and per function | `obj/firmware.map` |
| `boot` | the resident boot chunk baked into the image | `firmware/gen/boot_lc.h` |
| `bytecode` | stripped `.lc` size of every lib module | host `luac` (Docker) |
| `lines` | lines per source area, vendored called out | the source tree |
| `functions` | `src/main.c`'s function count — the one that keeps moving | `src/main.c` |

`task lua:lib:size` is a view over the `bytecode` section rather than a second
implementation of it: two counts of the same thing is how the documented figures came
apart in the first place.

## Three things worth knowing

**It refuses to report stale numbers.** The flash figures come from a build, so a map
or ELF older than any source it was built from is an error naming the offending file,
not a footnote. Yesterday's flash total looks exactly like today's until someone acts
on it.

**An empty parse is an error too.** Every section asserts it measured something —
zero bytes of flash, no objects, no modules, no functions. A tidy report of zeros is
indistinguishable from a measurement, which is the vacuous pass the repo's testing
rule warns about.

**Flash comes from the ELF, not the map.** `ld` prints a `load address` in flash for
`.bss` as well, so billing every section with one would charge the budget ~5 KB of
zero-init RAM. `SHT_NOBITS` is the bit that settles it, so the section table is read
directly (pure Python — the toolchain container is not needed to read an ELF).

Per-object bytes are *attributed*, and they sum slightly over the linked total:
mergeable string sections are billed to every object that contributed one but stored
once. Use `flash` for the budget and `objects` for where it went.

## The counting rules

Line counts define an area as `src/<area>/*.c` plus the headers that declare it,
`inc/<area>/*.h`. The loose headers directly in `inc/` belong to no single area
(`common.h` is included by 41 files) so they form their own row instead of being
folded into whichever directory a hand-written sweep happened to reach. Functions are
definitions whose return type starts at column 0 — this tree's style throughout —
which is what separates them from prototypes and from `if (…)`.

Both rules are mechanical and both are pinned by `test/run.py`, which drives fixtures
with known answers (a wrapped section name, `*fill*` padding, an archive member, a
discarded input section, a `.bss` advertising a flash load address) rather than
comparing two runs of the tool against each other.

No `Dockerfile`: `measure.py` is stdlib-only and reads the source tree plus build
products that Docker already produced. The one figure that needs a container is the
bytecode, and it borrows the sibling [`tools/luac`](../luac/README.md) image.
