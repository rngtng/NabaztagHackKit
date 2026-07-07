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
task build:firmwareV2            # -> bin/hello.{elf,hex,bin}  (M0, toolchain check)
task build:firmwareV2 APP=blink  # -> bin/blink.{elf,hex,bin}  (M1, LED blink)
task build:firmwareV2 APP=lua    # -> bin/lua.{elf,hex,bin}    (M4, Lua 5.4 REPL)
```

`arm-none-eabi-gcc` + newlib-nano, `-mcpu=arm7tdmi -mthumb -mthumb-interwork`,
linked against [`sys/ml67q4051.ld`](sys/ml67q4051.ld) with our own startup
(`-nostartfiles`) and `--gc-sections` (drops unreferenced code; the vector table
and startup are `KEEP`-guarded in the linker script). One selectable app from
`src/app/` at a time (`APP=`).

## Lua runtime (M4, #92)
`APP=lua` boots **PUC-Rio Lua 5.4** ([`lua/`](lua/), vendored - see
[`PROVENANCE.md`](../../PROVENANCE.md)) and drops into a REPL. The glue lives in
[`src/app/lua.c`](src/app/lua.c): a trimmed `luaL_openlibs`, an embedded demo
chunk, the REPL loop (`luaL_loadstring`/`lua_pcall`), and the two things a hosted
Lua assumes but bare metal lacks -

- **Console:** newlib `_read`/`_write` route stdin/stdout through **ARM
  semihosting** (`svc 0xAB`), the M3 (#91) no-UART console. So `print()` and the
  prompt reach the GDB/OpenOCD console on hardware and the simulator off it.
- **Heap:** `_sbrk` hands out the **1 MB external RAM** window (`0xD0000000`, set
  up by `init.s`' EMC init) - the 16 KB internal RAM is too small for a Lua state.

Config, tuned to the **124 KB internal-flash budget** (`luaconf.h` sets
`LUA_32BITS` - 32-bit int + 32-bit float, no FPU/`double`/`long long`):

- **Stdlib = base + string + table** only. Dropped: `math` (~16 KB of libm trig),
  `io`/`os`/`package`/`debug`/`loadlib`/`coroutine`/`utf8`. `base`'s
  `dofile`/`loadfile` are removed in `lua/lbaselib.c` (no filesystem).
- **Integer math is exact; float *printing* does not work yet.** newlib-nano
  omits float support from `printf` unless forced with `-u _printf_float`, which
  costs ~7.6 KB and does **not** fit (even base+string+float overflows 124 KB). So
  `print(1+1)` → `2` (the #92 DoD, exact), `10//3` → `3`, but a float value like
  `1/2` currently prints as `.0`. Float *arithmetic* is still correct internally;
  only the string rendering is stubbed. Proper float output waits until code moves
  off the internal flash (M6 external flash) or a smaller custom formatter lands.

Result: `bin/lua.elf` ~125 KB `.text` (fits 124 KB). See [Simulate](#simulate-no-hardware).

## Simulate (no hardware)
Run the compiled ELF in an instruction-level simulator ([`sim/`](sim/),
Unicorn Engine, issue #96) - no JTAG, no device:

```sh
task simulate:firmwareV2                        # run bin/hello.elf, report reaching main
task simulate:firmwareV2 APP=blink ARGS=-v      # -v logs every peripheral (MMIO) write
task simulate:firmwareV2 APP=blink ARGS="-v -n 8000000"  # bigger budget: see a full blink cycle
task simulate:firmwareV2 APP=lua ARGS="-n 60000000"      # M4: boots Lua, runs print(1+1) over the semihost console
```

Lua needs a large instruction budget (interpreter bring-up); the semihosting
console output is printed in the run summary. To drive the **REPL** yourself, use
`task repl:firmwareV2` - a live prompt, or a script fed in:

```sh
task repl:firmwareV2                                               # live interactive prompt (type Lua, Ctrl-D to exit)
task repl:firmwareV2 SCRIPT=src/firmwareV2/examples/repl-demo.lua  # feed a .lua file, print the transcript
```

The live prompt reads your terminal via semihosting `SYS_READC` (needs a TTY);
`SCRIPT=` feeds the file verbatim (real newlines, no escaping). Each REPL line is
its own chunk, so `local`s do not persist across lines - use globals to carry
state (same as the stock `lua` prompt). Under the hood these mount `bin/lua.elf`
into the [`sim/`](sim/) container; the raw simulator also takes `--input
'…\n…'` (inline, backslash escapes) and `--interactive` if you drive it directly.

It maps the real memory regions, loads the ELF, runs from `Reset_Handler`, stubs
peripheral pages (logging GPIO/LED writes), models **instant SPI completion** (a
data-register write sets the `SPIF` transfer-complete flag, so the LED/flash/audio
drivers' busy-waits return), and implements ARM semihosting on the SWI vector so a
future console/REPL (M3/M4) runs in software. It models only stubbed peripherals -
**no timing, audio, WiFi, RFID, or real timers** - so it validates code paths +
GPIO + SPI framing + console, not analog behaviour. Because it has no timer model,
delays must be software busy-loops to be observable (see `blink`, below). QEMU
isn't used: it has no ML67Q4051 machine and our memory map doesn't fit its stock
boards.

The `blink` app (M1) uses a software busy-loop delay, so a full RED-on/off cycle
costs several million instructions; run with a larger `-n` budget (above) to see
it toggle. The default budget still proves it reaches `main()`, brings up the I/O
lines, and drives the LED SPI bus.

## Layout
```
sys/                ARM7TDMI startup, linker, OKI register defs (copied from src/firmware)
  ml67q4051.ld      memory map
  asm/init.s        reset vector, clock + EMC init, per-mode stacks, .data/.bss, -> main()
  asm/*.s           SWI + reentrant IRQ/FIQ handlers
  src/irq.c         IRQ handler table + init
  inc/*.h           ml674061.h (GPIO/SPI/IRQ regs), irq.h
inc/common.h        GPIO/register macros (debug/UART include stripped - no UART here)
inc/hal/{led,spi}.h HAL headers (copied from src/firmware)
src/hal/spi.c       SPI0/SPI1 low-level access (copied from src/firmware)
src/hal/led.c       TLC594x RGB LED driver over SPI (copied from src/firmware)
src/app/hello.c     M0 toolchain-check app (spins; proves startup reaches main)
src/app/blink.c     M1 LED-blink app (#89) - first peripheral binary; blinks the nose (LED_RGB_5) red
src/app/ledmap.c    LED-map probe (#93) - lights all five LEDs distinct colours at once to read the physical map
src/app/lua.c       M4 Lua 5.4 REPL app (#92) - openlibs + REPL + semihosting syscalls + ExtRAM sbrk
lua/                vendored PUC-Rio Lua 5.4 core (#92); build compiles a subset (see Makefile LUA_CORE/LUA_LIB)
sim/                Unicorn instruction-level simulator (#96) - run the ELF, no hardware
```
The `lua/` tree is vendored upstream (PUC-Rio) - see [`PROVENANCE.md`](../../PROVENANCE.md).
The `sys/` tree, `inc/common.h`, and the `hal/` sources are **copied** from
`src/firmware` (the vendored `nabgcc` port) - see [`PROVENANCE.md`](../../PROVENANCE.md).
This layer is otherwise self-contained and does not depend on `src/firmware` at
build time.

## Milestones (see the #87 sub-issues)
| | | Issue | Status |
|---|---|---|---|
| M0 | Scaffold build layer | #88 | done (hardware-confirmed: PC parks in `main`) |
| M1 | Bare-metal LED blink | #89 | done (sim + hardware) - see LED-map note below |
| M2 | JTAG flash workflow (Task targets) | #90 | done (`task flash:firmwareV2`) |
| M3 | Semihosting console feasibility | #91 | |
| M4 | Lua 5.4 core + REPL | #92 | done (sim); hardware confirm pending M3 |
| M5 | Lua bindings: LEDs, buttons, ears | #93 | needs the LED-map fix first (see M1 note) |
| M6 | Lua binding: AT45DB161B flash | #94 | |
| - | tooling: Unicorn simulator | #96 | first cut done |

## Flashing
```sh
task flash:firmwareV2            # APP=hello (M0)
task flash:firmwareV2 APP=blink  # M1, visible LED blink
```
Host-side (JTAG can't run in Docker), via a Raspberry Pi bridge. Builds, ships
this repo's configs + ELF to the Pi, drives OpenOCD + gdb, verifies the write.
Setup + wiring + the manual fallback: [`tools/openocd/README.md`](../../tools/openocd/README.md).

## LED map (verified on hardware, feeds M5 #93)
The raw `LED_RGB_n` -> physical map (`led.h`, inherited from `src/firmware`) was
unlabeled and the old `blink` comment wrongly called channel 1 the "nose".
Verified on board `LLC2_4c` with the `ledmap` probe (`task flash:firmwareV2
APP=ledmap` lights all five a distinct colour at once):

| Channel | Physical |
|---|---|
| `LED_RGB_1` | belly (upper) |
| `LED_RGB_2` | belly bottom |
| `LED_RGB_3` | belly left |
| `LED_RGB_4` | belly right |
| `LED_RGB_5` | **nose** |

`blink` now drives `LED_RGB_5` (the actual nose). Still open for M5: `led.c`'s
`convled[]` applies a *separate* logical->physical remap for `set_led()`; that
logical numbering is unverified and should be pinned down before the Lua LED
binding.

> ⚠️ **Brick risk:** never erase or program internal flash without a verified
> full backup first. IDCODE `0x3f0f0f0f` appearing over JTAG = the CPU is alive.
