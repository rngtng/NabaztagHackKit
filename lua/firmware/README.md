# lua/firmware — Lua on the Nabaztag:tag (bare metal, JTAG-only)

An alternative **Layer 0**: a bare-metal **PUC-Rio Lua 5.4** runtime on the stock
hardware, replacing the mtl track's C-bytecode VM. Not eLua (dormant, Lua 5.1, its
board layer duplicates our HAL, and its RAM trick is moot given 1 MB ExtRAM). Tracking
issue: [#87](https://github.com/rngtng/NabaztagHackKit/issues/87).

## Design principles

Five principles ([#183](https://github.com/rngtng/NabaztagHackKit/issues/183), from
embedded-Lua practice - ArduPilot, rusEFI, Lua-RTOS) govern the C/Lua split, memory/error
budgets, and remote loading. **Binding on new work: a change that breaks one needs a
stated reason.** `(established)` = already in the code; `(target)` = being built toward.

1. **Layered API - HAL in C, behaviour in Lua, narrow seam.** *(established)* RFID, ear
   motors, LED PWM, sound DMA stay in C (`src/hal/`); Lua sees only high-level primitives -
   the [`nab` module](#the-nab-module). New hardware gets a C driver + a thin `nab.*`
   binding, never a register poke or timing loop in Lua.
2. **Minimal, event-driven core - cooperative, never Lua in an ISR.** *(target)* End state:
   a single C `while(1)` draining an event queue (RFID/timer/button) and calling Lua via
   `lua_pcall`. Today's runtime polls (REPL + `run()` demo loops); the hard rule already
   holds - no interrupt handler ever calls into Lua.
3. **Explicit memory + error budget.** *(partly established)* Flash is tracked to the byte
   (124 KB budget, `-Werror`, parser-less bytecode-only image #128). Every chunk runs under
   `lua_pcall`, so a script fault returns to the prompt instead of crashing the rabbit.
   *Target:* a fixed Lua-heap cap and error paths hardened so a broken remote script can't
   wedge core functions.
4. **Partial-update-friendly script structure.** *(target, ties to wifi)* Remote loading
   should swap small Lua/`luac` payloads, never reflash. Design a fixed script-slot layout;
   keep the C HAL + boot core independent of any loaded script.
5. **Sandbox from the start.** *(partly established)* Remote scripts reach only the `nab`
   API. Stdlib is trimmed to `base + string + table` - no `os`/`io`/`package`/`debug`/
   `loadlib`, `dofile`/`loadfile` removed - so no `os.execute`, no raw memory/filesystem
   access by construction. The parser-less image (#128) hardens this further: with no
   on-device compiler the rabbit cannot `eval` arbitrary source, only run bytecode it is
   handed. Every new binding is a bounded `nab.*` call; don't re-add a general-purpose
   library without a security review.

The driver-buildout sub-issue [#184](https://github.com/rngtng/NabaztagHackKit/issues/184)
maps these onto C subsystems. Two structural gaps:

- **Timer/scheduler is the missing substrate for principle 2.** No timer subsystem yet
  (every `ms` delay is a busy-loop). System ticks + a cooperative scheduler are the
  prerequisite for turning polling scripts into `lua_pcall` callbacks.
- **Script slots need versioning + rollback (principles 3-4)** with no storage on
  `LLC2_4c`: there is **no external flash** (#94, `CS_FLASH` unpopulated), so a slot region
  must be carved from the ~24 KB free internal flash or the volatile 1 MB ExtRAM.

> ⚠️ #184's hardware list is partly web-sourced and **contradicts the `LLC2_4c` board**: it
> claims LEDs via an `MCP23017` I2C expander (actually a **TLC594x over SPI**, `src/hal/led.c`)
> and ears via `L293D` (actually **OKI FTM PWM**, `src/hal/motor.c`). Neither part exists
> here. Trust the [teardown](../../docs/hardware-dissection.md) and a probe, not a forum post.

## Hardware
- MCU: OKI **ML67Q4051**, ARM7TDMI @ 33 MHz (no FPU, no Thumb-2, vectors at `0x0`)
- Internal flash `0x08000000`, 124 KB usable (last sector = config)
- Internal RAM `0x10000000`, **16 KB** (too small for Lua)
- External RAM `0xD0000000`, **1 MB** (Lua heap/data live here)
- Debug: 8-pin JTAG + **UART0 TX** (PB0=TX/PB1=RX, 38400 8N1 - the UART
  peripheral clock is a measured **8 MHz**, so 115200 is unreachable; #203).
  UART is bidirectional (input + output).
- Board revision `PCB_RELEASE LLC2_4c` (`inc/common.h`) - one of three tag sub-revisions
  (`LLC2_2`/`LLC2_3`/`LLC2_4c`), the one hardware-verified below. Pin diffs:
  [PCB revisions](../../docs/hardware-dissection.md#pcb-revisions-pcb_release).

## Build
Host needs only Docker + Task; the ARM toolchain lives in the Docker image. One app from
`src/app/` at a time (`APP=`).

```sh
task lua:firmware:build            # -> bin/hello.{elf,hex,bin}  (toolchain check)
task lua:firmware:build APP=blink  # -> LED blink
task lua:firmware:build APP=lua    # -> Lua 5.4 REPL
```

`arm-none-eabi-gcc` + newlib-nano, `-mcpu=arm7tdmi -mthumb -mthumb-interwork`, linked
against [`sys/ml67q4051.ld`](sys/ml67q4051.ld) with our own startup (`-nostartfiles`) and
`--gc-sections`. Our sources build warning-clean and **`-Werror`-enforced** under `-Wall
-Wextra -Wpedantic -Wpointer-arith -Wcast-align` (`-Wcast-align` matters on ARM7TDMI: an
unaligned 32-bit load rotates silently instead of faulting). The vendored Lua core is
exempt (`-Wno-cast-align -Wno-error`) - not our code to fix; see the `Makefile`.

## Lua runtime
`APP=lua` boots **PUC-Rio Lua 5.4** ([`lua/`](lua/), vendored - see
[`PROVENANCE.md`](../../PROVENANCE.md)) into a REPL. Glue in [`src/app/lua.c`](src/app/lua.c).
Two things bare metal lacks:

- **Console:** newlib `_read`/`_write` route stdin/stdout through **UART0**
  (`src/hal/uart.c`, 38400 8N1). `print()` and the prompt go out `putch_uart`/`_write`;
  REPL input comes in `getch_uart`/`_read` (EOF is EOT, `0x04`). Both directions run over
  UART on hardware (no JTAG session, no CPU halts) and the simulator models the UART0
  console. The old JTAG console path was fully removed in #207 (UART RX landed there too).
  Read it on the Pi rig: see
  [`../tools/openocd/README.md`](../tools/openocd/README.md#uart-console-tx-only-38400-8n1-203--preferred-for-output).
- **Heap:** `_sbrk` hands out the **1 MB ExtRAM** window (`0xD0000000`, set up by `init.s`);
  16 KB internal RAM is too small.

Tuned to the **124 KB flash budget** (`luaconf.h` sets `LUA_32BITS` - 32-bit int + float,
no FPU/`double`):

- **Stdlib = base + string + table** only. Dropped `math`/`io`/`os`/`package`/`debug`/
  `loadlib`/`coroutine`/`utf8`; `dofile`/`loadfile` removed (no filesystem).
- **Integer math is exact; float *printing* is approximate.** `1+1`→`2`, `10//3`→`3`. A float
  renders as integer part + `.0`, so whole floats are right (`2^10`→`1024.0`) but fractional
  digits drop (`1/2`→`0.0`). Float *arithmetic* is correct internally; only decimal rendering
  is stubbed - a real dtoa is future work.
- **Number I/O is newlib-free (#106):** compact decimal parser vs `strtof`, libm-free `^`/`%`
  (fractional `^`→NaN), in-tree `snprintf`/`vsnprintf` - so number formatting doesn't pull
  newlib's FILE layer.
- **Double-free (#213):** no float crosses a variadic call on the device - C default argument
  promotion would turn it into a `double` and link libgcc's double soft-float (~2.4 KB). Floats
  print via the non-variadic `luai_num2str`; `string.pack`'s C-`double` `'d'` code and
  `string.dump`/`ldump.c` (the device only *loads* bytecode) are compiled out behind
  `-DLUA_NOPARSER`.

`bin/lua.elf` uses ~110,900 B of 124 KB (**~15.7 KB free**; ~27 KB of that growth is
the M11 USB + 802.11/WPA wifi stack, ~0.8 KB the #216 raw-frame/AP bindings). Newlib's stdio FILE layer stays out only
because [`src/libc_shim.c`](src/libc_shim.c) provides local `rand`/`srand`/
`__assert_func` — the vendored net stack's `rand()` otherwise drags ~9 KB of
vfprintf/FILE machinery back in via newlib's asserting archive members. This is the
**only** image and it is **parser-less by design** (decided in
[#128](https://github.com/rngtng/NabaztagHackKit/issues/128)): `lparser`/`llex`/`lcode` are
dropped (~18.9 KB), so the rabbit runs *only* `luac` bytecode (`lundump` resident). There is
no on-device compiler - all Lua, including every REPL line and the resident boot chunk, is
compiled *off-device* by a `LUA_32BITS`-matched host `luac`. The freed budget is what lets
wifi C (~26 KB) fit.

### Off-device `luac` pipe
The host half lives in [`../tools/luac/`](../tools/luac/): a `luac` built from *this* `lua/`
tree + `luaconf.h`, so its bytecode matches what the rabbit's `lundump.c` accepts (4-byte
int/float/instruction). Building from the vendored tree - not a distro `luac` - is what keeps
the header sizes aligned; full rule in [`../tools/luac/README.md`](../tools/luac/README.md).

```sh
task lua:firmware:luac SOURCE=foo.lua OUT=foo.lc   # compile to stripped device bytecode
task lua:firmware:test:luac                        # golden-transcript test of the bytecode pipeline
```

Since bytecode contains `\n`/NUL and the console is line-oriented, the REPL accepts a
**frame**: a `#LC:<len>` header line + `2*len` hex chars. `load_lc_frame` (`src/app/lua.c`)
decodes and runs it; a non-frame line is rejected (there is no parser). The host tools:
`replpipe.py` frames a `.lua`/`.lc` file, `embed.py` bakes the boot chunk into
`gen/boot_lc.h`, and `luash.py` is the live-REPL client (compiles each typed line
off-device). A source line typed at a bare terminal will not run.

## Simulate (no hardware)
Run the ELF in an instruction-level simulator ([`../tools/simulator/`](../tools/simulator/),
Unicorn Engine, #96) - no JTAG, no device:

```sh
task lua:firmware:simulate                          # run bin/hello.elf, report reaching main
task lua:firmware:simulate APP=blink ARGS=-v        # -v logs every peripheral (MMIO) write
task lua:firmware:simulate APP=lua ARGS="-n 60000000"  # boots Lua, runs print(1+1) over the modelled UART console
```

To drive the **REPL**: `task lua:firmware:repl` (live prompt - `luash.py` compiles each line
you type off-device to bytecode, then pipes it to the modelled UART console) or feed a file:

```sh
task lua:firmware:repl SCRIPT=apps/repl-demo.lua        # compile + feed a .lua file, print transcript
task lua:firmware:repl SCRIPT=apps/foo.lc               # feed prebuilt .lc bytecode
```

Each REPL line is its own chunk, so `local`s don't persist - use globals (same as stock
`lua`). The simulator maps the real memory regions, runs from `Reset_Handler`, stubs
peripheral pages, models **instant SPI completion** (a data-register write sets `SPIF`), and
models the **UART0 console**. It has **no timing, audio, WiFi, RFID, or real timers**, so it
validates code paths + GPIO + SPI framing + console, not analog behaviour. Delays must be
software busy-loops to be observable. **DREQ (VS1003 ready) and the ADC completion bit are
unmodeled** - any bounded busy-wait on them (`nab.play`, `nab.wheel`) spins to its cap in-sim
and is hardware-only. QEMU isn't used (no ML67Q4051 machine, memory map doesn't fit).

## Layout
```
sys/                ARM7TDMI startup, linker, OKI register defs (copied from mtl/firmware)
  ml67q4051.ld      memory map
  asm/init.s        reset vector, clock + EMC init, per-mode stacks, .data/.bss -> main()
  asm/*.s           SWI + reentrant IRQ/FIQ handlers
  src/irq.c         IRQ handler table + init
  src/tick.c        1 ms system tick - the first live IRQ; drives DelayMs for the USB stack
  inc/*.h           ml674061.h (GPIO/SPI/IRQ regs), irq.h
inc/common.h        GPIO/register macros (UART include stripped - no UART here)
inc/hal/*.h         HAL headers (copied from mtl/firmware)
src/hal/spi.c       SPI0/SPI1 low-level access
src/hal/led.c       TLC594x RGB LED driver over SPI
src/hal/button.c    head-button read (polled active-low GPIO on P3.1)
src/hal/audio.c     VS1003 codec over SPI0 - SCI r/w, volume, amp, sine test, SDI playback
src/hal/adc.c       ADC ch.2 read (the back wheel)
src/hal/i2c.c       I2C bus - OKI bring-up + polled master read/write
src/hal/rfid.c      CRX14 RFID coupler over I2C - anti-collision scan + UID read
src/hal/motor.c     ear motor + encoder driver - FTM PWM drive + pulse-capture position, no IRQ
src/hal/uart.c      TX-only UART0 @38400 8N1 (#203) - polled putch/putst, no RX yet
src/usb/            USB host stack (#143) - ML60842 OHCI hcd/hcdmem + usbctrl + enumeration
src/app/*.c         one app per binary (see APP=); *probe.c are hardware bring-up probes
lua/                vendored PUC-Rio Lua 5.4 core; build compiles a subset (Makefile LUA_CORE/LUA_LIB)
../tools/simulator/ Unicorn instruction-level simulator (#96)
../tools/luac/      host luac matching the device's bytecode format (#133)
```
`sys/`, `inc/common.h`, and `hal/` are **copied** from `mtl/firmware` (the vendored `nabgcc`
port) - a register fix there may apply here, grep the sibling. Otherwise self-contained; no
build-time dependency on `mtl/firmware`. `lua/` is vendored upstream. Both: see
[`PROVENANCE.md`](../../PROVENANCE.md).

## Milestones
Sub-issues of [#87](https://github.com/rngtng/NabaztagHackKit/issues/87). "HW" = confirmed
on board `LLC2_4c`; "sim" = simulator-only, hardware confirmation pending.

| | Milestone | Issue | Status |
|---|---|---|---|
| M0 | Scaffold build layer | #88 | HW (PC parks in `main`) |
| M1 | Bare-metal LED blink | #89 | HW + sim |
| M2 | JTAG flash workflow | #90 | done (`task lua:firmware:flash`) |
| M3 | UART console | #91 | HW (console became UART0 in #207) |
| M4 | Lua 5.4 core + REPL | #92 | HW + sim |
| M5 | Bindings: LEDs, button, ears | #93 | LEDs + button HW; ears -> M10 |
| M6 | Binding: AT45 flash | #94 | **N/A** - no external flash on `LLC2_4c` (probed `id`/`status`=0). Reverted. |
| M7 | Reclaim flash budget | #106 | HW - 48 B -> ~32 KB free by moving console + number I/O off newlib |
| M8 | Audio - VS1003 codec | #116 | HW - `nab.beep` audible; VS1003B on SPI0 |
| M9 | RFID - CRX14 over I2C | #117 | sim - flash `rfidprobe` to confirm before trusting `nab.rfid()` |
| M10 | Ear motors - PWM + encoder | #118 | HW at full speed (see `nab.ear_*` caveats) |
| M11 | WiFi - USB host + RT2501 | #119 | open epic. **M11a done, HW:** USB host stack ported, first live IRQ (1 ms tick), `usbprobe` enumerates the dongle (VID:PID `0db0:6877`, RT2501). Next: rt2501usb driver + 802.11 (prereq #125). |
| - | Unicorn simulator | #96 | first cut done |
| - | `nab.play`/`nab.tone`/`nab.wheel` + wheel-click/jack probe | #123 | sim - hardware-only paths, see below |
| - | UART0 TX bring-up | #203 | HW - `uartprobe` banner read @38400 on the Pi serial link; RX + `nab.uart` + UART console open |

## Flashing
```sh
task lua:firmware:flash            # APP defaults to lua
task lua:firmware:flash APP=blink  # visible LED blink
```
Host-side (JTAG can't run in Docker), via a Raspberry Pi bridge: builds, ships configs + ELF
to the Pi, drives OpenOCD + gdb, verifies. Setup + wiring:
[`../tools/openocd/README.md`](../tools/openocd/README.md).

> ⚠️ **Brick risk:** never erase or program internal flash without a verified full backup
> first. IDCODE `0x3f0f0f0f` over JTAG = the CPU is alive.

## LED map (verified on hardware)
The raw `LED_RGB_n` -> physical map (`led.h`, inherited from `mtl/firmware`) was unlabeled;
verified on `LLC2_4c` with the `ledmap` probe:

| Channel | Physical | | Channel | Physical |
|---|---|---|---|---|
| `LED_RGB_1` | belly (upper) | | `LED_RGB_4` | belly right |
| `LED_RGB_2` | belly bottom | | `LED_RGB_5` | **nose** |
| `LED_RGB_3` | belly left | | | |

`blink` drives `LED_RGB_5` (nose). `nab.led` uses this raw `set_led_rgb` map by name, so it
doesn't depend on `led.c`'s `convled[]` logical remap (unverified, used only by the unused
`set_led`).

## The `nab` module
`APP=lua` exposes hardware to Lua via a built-in `nab` module (registered in `src/app/lua.c`):

```lua
nab.led(name, r, g, b)        -- name: nose|belly|left|right|bottom; r/g/b 0..127
nab.button()                  -- -> true while the head button is held (polled, undebounced)
nab.beep(freq, ms)            -- VS1003 sine test: freq = pitch byte 0..255, ms ~duration
nab.volume(v)                 -- 0 = loudest .. 254 = quietest (SCI_VOLUME)
nab.play(data)                -- stream bytes (WAV/MP3/...) over SDI - real decoded audio
nab.tone()                    -- -> a tiny built-in 8-bit PCM WAV (~200ms square), for nab.play
nab.wheel()                   -- -> 0..255, ADC ch.2 (the back wheel, believed a pot)
nab.rfid()                    -- -> lowercase hex UID string, or nil if no tag
nab.ear_move(n, speed, dir)   -- n: 1|2 (1 = left ear); speed 0..255; dir "forward"|"reverse"
nab.ear_stop(n)               -- n: 1|2
nab.ear_pos(n)                -- n: 1|2 -> raw wrapping 16-bit encoder pulse count
nab.wifi(ssid [, psk])        -- join an AP (WPA/WPA2 or open) -> true | nil, msg (M11)
nab.wifi_ap(ssid [, ch])      -- master (AP) mode: beacon an OPEN network on ch (default 1) (#216)
nab.wifi_send(dst_mac, data)  -- raw data frame at the 802.3 payload seam; dst_mac = 6-byte string
nab.wifi_recv([timeout_ms])   -- -> src_mac, payload | nil; bounded main-loop RX buffer
```

HW-verified (M5/M8): `nab.led` lights each named LED, `nab.button()` tracks the physical
button, `nab.beep()` is audible. Caveats:

- **`nab.beep` bypasses `SCI_VOLUME`** (the sine test is a fixed-level diagnostic tone), so
  `nab.volume` has no effect on it. `ms` is a rough CPU busy-loop (no timer yet).
- **`nab.ear_*` HW-verified at full speed only.** Both motors drive and encoders count. The
  `speed` parameter is unverified (#179) - `earprobe` now sweeps 255->20 and reports encoder
  delta; run it to find where movement stops before trusting partial speed. A bug fixed during
  bring-up: `init_ears` was missing the `PORTSEL3` pin-mux (`0x05550000`) that routes PF0-PF5
  to FTM; without it the PWM pins stayed GPIO and the motors were silent. `nab.ear_pos` reads
  the free-running `FTMnC` counter (not `FTMnGR`); it's a raw edge count, not an absolute angle
  (hole-counting arrival detection lives at the Forth layer, out of scope here).
- **`nab.play`/`nab.tone`/`nab.wheel` (#123) are unverified on hardware and unrunnable in-sim**
  (DREQ/ADC unmodeled). `nab.play` streams over SDI so `nab.volume` actually attenuates (VS1003B
  decodes MP3/WAV per the teardown); the SDI mechanism itself is M8-proven. `nab.wheel` ports
  `mtl/firmware`'s ADC ch.2 sequence. Flash + listen/probe to confirm.
- **`nab.rfid()` (#117) built + sim-verified, not HW-confirmed.** A documented chip isn't a
  *responding* chip until probed (see the M6 AT45 lesson) - run `rfidprobe` first. Reliability
  fix (#180): `hal/rfid.c` now tracks field state and drops/raises the RF field once instead of
  per-poll (passive tags were power-cycled every call); the settle-delay tuning + an
  event-shaped `nab.rfid()` are tracked in #195.

### SPI0 RX-FIFO + DREQ gotcha
`WriteSPI` (SPI0) clocks in a byte per write but never consumes it, so a run of SCI writes
fills the RX FIFO and shifts later `vlsi_read_sci` results - `audio.c` **drains the RX FIFO at
the end of every `vlsi_write_sci`**. DREQ also dips right after those writes, so the SDI
control feed **waits for DREQ** (bounded) per byte rather than aborting on DREQ-low. Both were
the root cause of an initially-silent `nab.beep`.
