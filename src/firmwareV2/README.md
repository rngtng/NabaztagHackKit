# firmwareV2 - Lua on the Nabaztag:tag V2 (bare metal, JTAG-only)

An alternative **Layer 0** for the Nabaztag:tag V2: a bare-metal Lua runtime on
the stock hardware, replacing the MTL C-bytecode VM (`src/firmware`). Tracking
issue: [#87](https://github.com/rngtng/NabaztagHackKit/issues/87).

**Runtime = PUC-Rio Lua 5.4** (not eLua). eLua is dormant (Lua 5.1, SCons), its
board-abstraction layer duplicates the HAL we already have in `src/firmware`, and
its RAM-saving trick (LTR) is moot given 1 MB of external RAM. See the runtime
rationale in [#87](https://github.com/rngtng/NabaztagHackKit/issues/87).

## Hardware
- MCU: OKI **ML67Q4051**, ARM7TDMI @ 33 MHz (no FPU, no Thumb-2, vectors at `0x0`)
- Internal flash `0x08000000`, 124 KB usable (last sector = config)
- Internal RAM `0x10000000`, **16 KB** (too small for Lua)
- External RAM `0xD0000000`, **1 MB** (Lua heap/data live here - M4)
- Debug: 8-pin JTAG only, **no UART broken out** on this board revision

## Build
Host needs only Docker + Task; the ARM toolchain lives in the Docker image.

```sh
task firmwareV2:compile            # -> bin/hello.{elf,hex,bin}  (M0)
task firmwareV2:compile APP=blink  # -> bin/blink.{elf,hex,bin}  (M1, later)
```

`arm-none-eabi-gcc` + newlib-nano, `-mcpu=arm7tdmi -mthumb -mthumb-interwork`,
linked against [`sys/ml67q4051.ld`](sys/ml67q4051.ld) with our own startup
(`-nostartfiles`). One selectable app from `src/app/` at a time (`APP=`).

## Layout
```
sys/                ARM7TDMI startup, linker, OKI register defs (copied from src/firmware)
  ml67q4051.ld      memory map
  asm/init.s        reset vector, clock + EMC init, per-mode stacks, .data/.bss, -> main()
  asm/*.s           SWI + reentrant IRQ/FIQ handlers
  src/irq.c         IRQ handler table + init
  inc/*.h           ml674061.h (GPIO/SPI/IRQ regs), irq.h
inc/common.h        GPIO/register macros (debug/UART include stripped - no UART here)
src/app/hello.c     M0 toolchain-check app (spins; proves startup reaches main)
```
The `sys/` tree and `inc/common.h` are **copied** from `src/firmware` (the vendored
`nabgcc` port) - see [`PROVENANCE.md`](../../PROVENANCE.md). This layer is otherwise
self-contained and does not depend on `src/firmware` at build time.

## Milestones (see the #87 sub-issues)
| | | Issue | Status |
|---|---|---|---|
| M0 | Scaffold build layer (this) | #88 | in progress |
| M1 | Bare-metal LED blink | #89 | |
| M2 | JTAG flash workflow (Task targets) | #90 | |
| M3 | Semihosting console feasibility | #91 | |
| M4 | Lua 5.4 core + REPL | #92 | |
| M5 | Lua bindings: LEDs, buttons, ears | #93 | |
| M6 | Lua binding: AT45DB161B flash | #94 | |

## Flashing
Not wired yet - lands in **M2** ([#90](https://github.com/rngtng/NabaztagHackKit/issues/90)).
Until then, flash `bin/*.elf` manually with OpenOCD + GDB per
[`tools/openocd/README.md`](../../tools/openocd/README.md).

> ⚠️ **Brick risk:** never erase or program internal flash without a verified
> full backup first. IDCODE `0x3f0f0f0f` appearing over JTAG = the CPU is alive.
