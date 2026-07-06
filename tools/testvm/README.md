# testvm

Native (x86) build of the firmware's own C bytecode VM (`src/firmware/src/vm`,
`src/firmware/src/net`), with the hardware-facing modules (`hal/`, `usb/`, `utils/`)
swapped for host stubs (`stubs/`). Loads real bytecode and runs it, so it catches VM
bugs the MTL simulator (a separate, independent implementation) can't see.

Vendored from `nabgcc/testvm/` — see `PROVENANCE.md` for the origin and the local
fixes needed to get it building/running (missing headers, a stray `dbg_buffer`
redefinition, a `consolestr` macro that isn't visible outside `vm/vlog.c`, and the
Taskfile's path assumptions).

## Usage

Build the boot bytecode first, then run the smoke test against it:

```
task build:boot
task test:firmware               # bounded run (default 5s); pass = exit 0 or timeout
task test:firmware DURATION=15   # longer window
```

To watch it run interactively instead of a bounded smoke test:

```
task testvm:simulate SOURCE=build/boot/dumpbc.c
```

`SOURCE` is a `dumpbc.c` file as written by `mtl_compiler` (raw bytecode, i.e.
`SIGN=false` — the same format the simulator loads, not the signed device-flash
format). A button push can be simulated by writing one byte to the `button.sock`
Unix datagram socket under `SOCK_DIR` (default `.`, or `build/` via `test:firmware`).

## Bug reproduction harness (issue #66)

`bugrepro.c` + `run-bugrepro.sh` build the real firmware VM sources under
AddressSanitizer and drive the exact buggy code paths for the memory-safety
issues flagged in issue #66. No bytecode file is needed — each scenario either
calls a firmware helper directly or hand-assembles a few opcodes and enters the
real `interpGo()`.

```
task test:firmware-bugs      # build + run all scenarios under ASan
```

Scenarios (selected internally by `bugrepro <name>`):

| scenario    | firmware code                       | bug                                              |
|-------------|-------------------------------------|--------------------------------------------------|
| `syscmp`    | `vlog.c` `sysCmp()`                 | length not clamped when one operand overruns → OOB read |
| `store`     | `vinterp.c` `OPstore`               | `(i>=0)||(i<VSIZE(p))` bounds guard is a tautology → OOB write |
| `setstruct` | `vinterp.c` `OPsetstruct`           | same tautology guard → OOB write                 |

This is a **regression guard**: it exits nonzero (and prints the ASan report
with the offending `vinterp.c`/`vlog.c` line) while a bug is still present, and
runs clean once the guard is fixed.

## VM internals cheat-sheet (for writing native tests)

Enough to add a new `bugrepro.c` scenario — or any native VM test — without
re-deriving the layout. Definitions live in `src/firmware/inc/vm/vmem.h`.

**One array holds everything.** `_bytecode` is literally `(uint8_t*)_vmem_heap`
(see `vloader.h`). The loader copies bytecode to the front of the heap; the heap
grows *up* from `_vmem_start`, the value stack grows *down* from
`_vmem_top = &_vmem_heap[VMEM_LENGTH]`. Consequence for ASan: an out-of-bounds
access is only caught when it leaves the `_vmem_heap` array entirely. A small
over-read into an adjacent block stays inside the array and is invisible — so to
make a bug ASan-visible, either drive the index *past the whole array* (see the
`store` scenario, index `VMEM_LENGTH`) or call the helper directly on separately
`malloc`'d buffers (see the `syscmp` scenario).

**Value tagging** (every stack word is a tagged 31-bit value):
- int `i`  ↔  `INTTOVAL(i)` = `i<<1`,  back via `VALTOINT` = `v>>1`
- heap ptr `p` ↔ `PNTTOVAL(p)` = `1+(p<<1)`, back via `VALTOPNT` = `v>>1`
- `ISVALPNT(v)` = `v&1` (low bit tags pointer vs int); `NIL = -1`
- a "pointer" `p` is a **word index** into `_vmem_heap`, not an address. Each
  block is `[header(3 words)][payload]`; `VSIZE(p)`/`VSIZEBIN(p)` = element/byte
  count, `VSTART(p)`/`VSTARTBIN(p)` = payload, `VFETCH/VSTORE` index the payload.

**Driving `interpGo()` without a bytecode file** (what `vm_run()` in `bugrepro.c`
does): write opcode bytes near the front of `_bytecode`; point `_bc_tabfun` at a
4-byte little-endian entry holding function 0's byte offset; set `_bc_nbfun=1`;
`vmemInit(start)` with `start` in **words** past your code; push `SYS_NB` NILs and
set `_sys_start`; then `VPUSH(INTTOVAL(0)); interpGo();` to call function 0. A
function body is `[narg:1][nloc:2 LE][opcodes…]`. Opcode numbers are in
`inc/vm/vbc.h`; immediates are little-endian (`OPint` = 4 bytes, `OPintb`/table
sizes = 1 byte). Watch operand order — e.g. `OPstore` pops value then index and
peeks the table, so push table, index, value.

**Other traps that cost time:** the VM never returns on its own (the smoke test
relies on a timeout); GC **reboots** (`sysReboot`) on out-of-memory rather than
returning an error; and the firmware VM (`src/firmware/src/vm/`) and the MTL
simulator VM (`tools/mtl_linux/src/vm/`) are **twin copies of the same code** —
a VM bug or fix almost always applies to both, so grep the sibling before
concluding a divergence is intentional.
