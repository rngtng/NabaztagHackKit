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
- **Integer math is exact; float *printing* is approximate.** `print(1+1)` → `2`
  (the #92 DoD, exact), `10//3` → `3`. Our custom formatter (M7.5, below) renders
  a float as its integer part + `.0`, so whole-valued floats are correct
  (`print(2^10)` → `1024.0`) but fractional digits are dropped (`1/2` → `0.0`,
  `5.5%2` → `1.0` though the value is `1.5`). Float *arithmetic* is correct
  internally; only the decimal string rendering is stubbed - a real dtoa is
  future work (the flash-cheap way was never the blocker; correctness is).
- **Number I/O is newlib-free (M7, #106).** To reclaim internal flash the number
  paths were moved off newlib entirely: decimal `string→number` uses a compact
  parser instead of `strtof` (M7.2 #108); `^` and float `%` are computed without
  libm (M7.3 #109) - integer exponents exact, fractional `^` returns NaN
  (`math.sqrt` etc. are unavailable anyway); and `snprintf`/`vsnprintf` are a
  compact in-tree implementation (M7.5 #114) so number formatting + `string.format`
  no longer pull newlib's buffered-FILE layer. Decimal float *parsing* is slightly
  looser than IEEE last-ulp; integer literals and `string.format` integer/string
  conversions are exact.

Result: `bin/lua.elf` `.text` **94976 B** of the 124 KB budget (**~32 KB free**
after M7 #106; was ~48 B). See [Simulate](#simulate-no-hardware).

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
src/hal/button.c    M5 head-button read (#93) - polled active-low GPIO on P3.1
src/hal/audio.c     M8 VS1003 codec over SPI0 (#116) - SCI r/w, volume, amp, sine test
src/app/hello.c     M0 toolchain-check app (spins; proves startup reaches main)
src/app/blink.c     M1 LED-blink app (#89) - first peripheral binary; blinks the nose (LED_RGB_5) red
src/app/ledmap.c    LED-map probe (#93) - lights all five LEDs distinct colours at once to read the physical map
src/app/console.c   M3 semihosting-console probe (#91) - svc 0xAB SYS_WRITEC, proves the no-UART console on hardware
src/app/audioprobe.c M8 VS1003 aliveness probe (#116) - reset + SCI_STATUS + VOLUME write/read-back
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
| M3 | Semihosting console feasibility | #91 | done - proven on hardware (needs a HW breakpoint at the SWI vector; recipe in tools/openocd) |
| M4 | Lua 5.4 core + REPL | #92 | done (sim + hardware): REPL runs on the rabbit over the M3 semihosting console |
| M5 | Lua bindings: LEDs, buttons, ears | #93 | LEDs + button done (`nab` module, hardware-verified); ears deferred (need an FTM timer/PWM/encoder subsystem, none in firmwareV2 yet) |
| M6 | Lua binding: AT45DB161B flash | #94 | **not applicable** - no external serial flash on the LLC2_4c board. Built `nab.flash` + an AT45 driver, then hardware read `id`/`status` = `0` (no device on `CS_FLASH`); the [teardown](../../docs/hardware-dissection.md) lists no flash chip and Violet's own `common.h` defines `CS_FLASH` only for LLC2_3. Reverted. |
| M7 | Reclaim internal flash budget | #106 | **done (hardware-verified)**: **48 B → ~32 KB free** by moving Lua's console + number I/O off newlib - custom decimal parser vs `strtof`/gdtoa (M7.2 #108), libm-free `^`/`%` (M7.3 #109), bare-metal `abort` (M7.4 #110), semihosting console (M7.1 #107), and an in-tree `snprintf`/`vsnprintf` (M7.5 #114) dropping the stdio FILE layer. |
| M8 | Lua audio - VS1003 codec | #116 | **done (hardware-verified)**: `nab.beep` plays an audible tone on the speaker. VS1003B confirmed on SPI0 (probe: SS_VER=3), trimmed driver ported (`src/hal/audio.c`). Beep is fixed-level (VS1003 sine test bypasses volume); volume-controlled PCM playback + the wheel/jack are follow-ups. |
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

`blink` now drives `LED_RGB_5` (the actual nose). The M5 `nab.led` binding uses
this raw `set_led_rgb` map (by name) directly, so it does not depend on `led.c`'s
`convled[]` - that separate logical remap (used only by the unused `set_led`)
stays unverified, harmless for now.

## Lua hardware bindings: the `nab` module (M5 #93, M8 #116)
`APP=lua` exposes hardware to Lua via a built-in `nab` module (registered in
`src/app/lua.c`; LED init mirrors `blink`, button read is `src/hal/button.c`,
audio is `src/hal/audio.c`):

```lua
nab.led(name, r, g, b)  -- name: nose|belly|left|right|bottom; r/g/b 0..127
nab.button()            -- -> true while the head button is held (polled)
nab.beep(freq, ms)      -- VS1003 sine test: freq = pitch byte (0..255), ms ~duration
nab.volume(v)           -- 0 = loudest .. 254 = quietest (VS1003 SCI_VOLUME)
```

Verified on hardware over the M3 semihosting console: `nab.led("nose",0,127,0)`
lights the nose green, each name maps to the right LED, and `nab.button()`
returns `false`/`true` tracking the physical button. `nab.beep()` produces an
audible tone on the rabbit's speaker (M8, #116). **Caveats:** `nab.beep` uses the
VS1003 built-in *sine test*, which is a **fixed-level** diagnostic tone that
bypasses `SCI_VOLUME` - so `nab.volume` has no effect on it (verified `0x00`
vs `0xFE` identical). `nab.volume` sets the register (which will attenuate
*decoded* audio once SDI/PCM playback lands) and `ms` is a rough CPU busy-loop
(no timer subsystem yet). Real, volume-controlled playback is a follow-up. See
also the SPI0 RX-FIFO/DREQ note below - the fix that made the beep reliable.
**Ears are deferred** - the
motors need an FTM timer/PWM + encoder-capture subsystem that firmwareV2 does not
have yet (it lives in `src/firmware`); that is a follow-up, not part of this cut.
The Lua app now has **~29 KB free** of the 124 KB (was ~48 B before M7 #106; see
the Lua runtime note) - room for the i2c+rfid / motor bindings that close the
gap to `src/firmware`.

### SPI0 RX-FIFO + DREQ (M8 gotcha)
`WriteSPI` (SPI0) clocks in a byte per write but never consumes it, so a run of
SCI writes fills the RX FIFO and shifts later `vlsi_read_sci` results. `audio.c`
therefore **drains the RX FIFO at the end of every `vlsi_write_sci`**. Separately,
DREQ dips right after those writes, so the SDI control feed **waits for DREQ**
(bounded) per byte rather than aborting on DREQ-low - otherwise the 8-byte
sine-start sequence is dropped and the beep is silent. Both were the root cause
of an initially-silent `nab.beep` that worked only when register reads happened
to drain the FIFO first.

### Hardware notes (unverified, from bring-up)
- The back **wheel** is almost certainly an analog pot on **ADC ch.2**
  (`get_adc_value` in `src/firmware`), not a GPIO and not in the speaker path -
  software would read it and apply `nab.volume`; it does not affect the
  fixed-level sine beep. The end-of-travel **click** may be a separate switch.
- The **audio-out jack** likely has a mechanical normalled switch that cuts the
  internal speaker when a plug is inserted (physical, not GPIO).
- Both are worth a dedicated probe (read ADC ch.2 while turning; GPIO scan for
  the click) - tracked separately, not part of M8.

> ⚠️ **Brick risk:** never erase or program internal flash without a verified
> full backup first. IDCODE `0x3f0f0f0f` appearing over JTAG = the CPU is alive.
