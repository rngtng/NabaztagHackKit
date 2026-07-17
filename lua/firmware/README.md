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
2. **Minimal, event-driven core - cooperative, never Lua in an ISR.** *(established, #195)*
   `src/event.c`: C pollers (debounced button, ~750 ms RFID scan) post edge events into a
   small fixed-size queue; the Lua layer drains it via `lua_pcall`'d callbacks
   (`nab.on`), from the REPL's idle loop or `nab.wait()`. The hard rule holds - no
   interrupt handler ever calls into Lua (the 1 ms tick ISR only counts).
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
maps these onto C subsystems. Remaining structural gap:

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

`bin/lua.elf` uses 109,648 B of 124 KB (**~16.9 KB free**; ~23 KB of that growth is
the M11 USB + 802.11/WPA2 wifi stack, ~0.8 KB the #216 raw-frame/AP bindings, 836 B
the #214 config-sector writer + binding, ~1.5 KB the #195 event core +
`nab.on`/`nab.wait`/`nab.time` bindings). The stack is **WPA2-CCMP only** (#124,
3,896 B reclaimed): HMAC-MD5, RC4 and every WEP/WPA1/TKIP path are gone - `nab.wifi`
joins open or WPA2-PSK(AES) networks and rejects anything else at scan/auth. Newlib's stdio FILE layer stays out only
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
task lua:compile SOURCE=foo.lua [OUT=foo.lc]       # compile to stripped device bytecode (layer-wide verb)
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
models the **UART0 console**. Beyond the 1 ms System Timer (below), it has **no timing, audio,
WiFi, RFID, or analog model**, so it validates code paths + GPIO + SPI framing + console, not
analog behaviour. Delays must be software busy-loops to be observable. **DREQ (VS1003 ready)
and the ADC completion bit are unmodeled** - any bounded busy-wait on them (`nab.play`,
`nab.wheel`) spins to its cap in-sim and is hardware-only. QEMU isn't used (no ML67Q4051
machine, memory map doesn't fit).

**System Timer + IRQ (#102).** The sim models the 1 ms System Timer and *delivers its
interrupt*: it runs the CPU in ~1 ms instruction slices (`INSNS_PER_MS`) and, between slices,
performs a real ARM7 IRQ entry (SPSR←CPSR, switch to IRQ mode, vector to the flash-resident
`0x18` handler) whenever the timer is enabled and interrupts are unmasked. The firmware's own
dispatcher then runs `timer_handler` → `led_fade_tick` - the *actual* fade code - so
`counter_timer` advances and LED **fades animate in the sim**, exactly as on hardware. Timing
is still approximate (instruction-count clock, not cycle-accurate); `nab.delay`'s busy-loop is
calibrated to roughly the same scale so fades and delays keep pace.

**Live LED view (#102).** `--leds` (or `task lua:firmware:simulate:leddemo`) reconstructs the 14-byte
dot-correction frame from the SPI1 byte stream (latched on the CS_LED rising edge), un-packs it
to five RGB LEDs, and draws them as an ANSI truecolor strip - a live in-place animation on a
TTY, or one line per distinct frame when piped. The run summary reports `timer IRQs` delivered
and `LED frames` rendered. The script is compiled to `#LC` bytecode before it is fed (the
firmware is parser-less, #128); the sim is paced to wall-clock time (`--speed`, default `1`)
so the animation is watchable rather than a host-CPU blur - lower it (`SPEED=0.25`) for a
close look.

```sh
task lua:firmware:simulate:leddemo                        # ../apps/led-demo.lua, real-time 5-LED view
task lua:firmware:simulate:leddemo SPEED=0.5              # half speed for a closer look
task lua:firmware:simulate:leddemo SCRIPT=path/to.lua N=200000000
```

## Layout
```
sys/                ARM7TDMI startup, linker, OKI register defs (copied from mtl/firmware)
  ml67q4051.ld      memory map
  asm/init.s        reset vector, clock + EMC init, per-mode stacks, .data/.bss -> main()
  asm/*.s           SWI + reentrant IRQ/FIQ handlers
  src/irq.c         IRQ handler table + init
  src/tick.c        1 ms system tick - the first live IRQ; counter_timer + DelayMs
  inc/*.h           ml674061.h (GPIO/SPI/IRQ regs), irq.h
inc/common.h        GPIO/register macros (UART include stripped - no UART here)
src/event.c         cooperative event core (#195) - fixed queue + button/RFID pollers
inc/hal/*.h         HAL headers (copied from mtl/firmware)
src/hal/spi.c       SPI0/SPI1 low-level access
src/hal/led.c       TLC594x RGB LED driver over SPI
src/hal/button.c    head-button read (polled active-low GPIO on P3.1)
src/hal/audio.c     VS1003 codec over SPI0 - SCI r/w, volume, amp, sine test, SDI playback
src/hal/adc.c       ADC ch.2 read (the back wheel)
src/hal/i2c.c       I2C bus - OKI bring-up + polled master read/write
src/hal/rfid.c      CRX14 RFID coupler over I2C - anti-collision scan + UID read
src/hal/motor.c     ear motor + encoder driver - FTM PWM drive + pulse-capture position, no IRQ
src/hal/uart.c      polled UART0 @38400 8N1 (#203/#207) - TX putch/putst + RX getch/rxrdy
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
| - | LED gamma + background fade engine | #102 | sim (fades animate); HW pending. Gamma-2.2 `led_pack`/`led_flush` re-synced from #45; a background fade engine driven off the M11a 1 ms tick (`sys/src/tick.c`), whose ring-osc reload was corrected (0xF830->0xFC18); adds `nab.led8`/`nab.fade`/`nab.delay` + `../apps/led-demo.lua` (`task lua:firmware:simulate:leddemo`). |
| - | Unicorn simulator | #96 | first cut done |
| - | `nab.play`/`nab.tone`/`nab.wheel` + wheel-click/jack probe | #123 | sim - hardware-only paths, see below |
| - | UART0 TX bring-up | #203 | HW - `uartprobe` banner read @38400 on the Pi serial link; RX + `nab.uart` + UART console open |
| - | `nab.config` - persist wifi creds in the config sector | #214 | built - HW verify pending (sim models flash reads only; write creds, power-cycle, read back) |
| - | Cooperative event core - `nab.on`/`nab.wait`/`nab.time` | #195 | built - HW verify pending (sim has no timer/RFID model; register `watch()` on the rig, place/remove a tag, press the button) |

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

`blink` drives `LED_RGB_5` (nose). `nab.led` uses this raw `set_led_rgb` map by name. The gamma
bindings (`nab.led8`/`nab.fade`) go through `led.c`'s `set_led`/`led_fade`, which apply the
`convled[]` logical remap - so `nab_led_logical()` inverts `convled[]` to hit the same physical
LEDs by name. All three land on the verified map above.

## LED gamma + background fades (#102)
`src/hal/led.c` was re-synced from `mtl/firmware` (PR #45): a **gamma-2.2** table with **no
low-end dead zone** (the old table crushed inputs 0..51 to 0, so fades snapped to black over the
bottom 20%), the generic `led_pack`/`led_flush` packer (verified **bit-identical** to the old
per-LED mask code over 2M random writes), and a **background fade engine**
(`led_fade`/`led_fade_tick`). The engine is advanced from the **1 ms System Timer tick**
(`sys/src/tick.c`) that M11a (#143) already brought up for the USB stack: `tick_handler()` now
also calls `led_fade_tick()`, and `init_hw()` in `src/app/lua.c` arms it with `init_irq()` ->
peripherals -> `init_tick()`. `led_fade_tick()` runs in the timer ISR (no main loop during the
REPL), so led.c's main-context writers mask that IRQ around their SPI flush
(`irq_disable_save`/`irq_restore`).

Wiring the fades up caught a **calibration bug** in the shared tick: it reloaded `TMRLR=0xF830`
(1 ms @ 32 MHz, copied from `mtl/firmware`), but V2 never runs `init_pll()` and clocks off the
**16 MHz ring oscillator**, so every tick - and thus every fade and `DelayMs` - ran ~2x slow on
hardware. Corrected to `0xFC18` (1 ms @ 16 MHz). (The tick only fires because M11a already moved
`main` to **System mode**, where `msr cpsr_c` can clear the I-bit that `__enable_interrupt()`
needs - `mtl/firmware`'s `swi` path is avoided because a SWI collides with the semihosting
console.)

`../apps/led-demo.lua` showcases both: a smooth breathe (fade all five up and back, no dead
zone) and a **pinball** ball that hops the belly ring leaving comet trails, with a red nose flash
per lap. It animates in the sim too (the sim delivers the timer IRQ - see Simulate):

```sh
task lua:firmware:simulate:leddemo            # ../apps/led-demo.lua with the live 5-LED view (sim)
task lua:firmware:flash APP=lua      # then run ../apps/led-demo.lua on real hardware
```

Sim is instruction-timed, not cycle-accurate, so a hardware pass to confirm the tick rate + a
good-looking fade is the open item on #102.

## The `nab` module
`APP=lua` exposes hardware to Lua via a built-in `nab` module (registered in `src/app/lua.c`):

```lua
nab.led(name, r, g, b)        -- name: nose|belly|left|right|bottom; r/g/b 0..127 (raw, no gamma)
nab.led8(name, r, g, b)       -- same LEDs, r/g/b 0..255 through the gamma-2.2 table (#102) - instant
nab.fade(name, r, g, b, ms)   -- background fade to r/g/b (8-bit gamma) over ms; returns immediately (#102)
nab.delay(ms)                 -- block ms (timed off the System Timer); paces animations while fades run
nab.button()                  -- -> true while the head button is held (polled, undebounced)
nab.beep(freq, ms)            -- VS1003 sine test: freq = pitch byte 0..255, ms ~duration
nab.volume(v)                 -- 0 = loudest .. 254 = quietest (SCI_VOLUME)
nab.play(data)                -- stream bytes (WAV/MP3/...) over SDI - real decoded audio
nab.tone()                    -- -> a tiny built-in 8-bit PCM WAV (~200ms square), for nab.play
nab.wheel()                   -- -> 0..255, ADC ch.2 (the back wheel, believed a pot)
nab.rfid()                    -- -> lowercase hex UID string, or nil if no tag (one live scan)
nab.on(name, fn|nil)          -- register/clear an event callback (#195): "button" -> fn(pressed)
                              --   on debounced edges; "rfid" -> fn(uid|nil) on tag arrive/leave
                              --   (registering starts the background ~750ms scan). Callbacks
                              --   fire while the REPL prompt idles or inside nab.wait().
nab.wait(ms)                  -- sleep ~ms on the 1 ms tick, running the event pump meanwhile
nab.time()                    -- -> ms since boot (wrapping 32-bit tick; 0 in the simulator)
nab.ear_move(n, dir)          -- n: 1|2 (1 = left ear); dir "forward"|"reverse" (full speed; see #179)
nab.ear_stop(n)               -- n: 1|2
nab.ear_pos(n)                -- n: 1|2 -> raw wrapping 16-bit encoder pulse count
nab.wifi(ssid [, psk])        -- join an AP (WPA2-CCMP or open; #124) -> true | nil, msg (M11)
nab.wifi_ap(ssid [, ch])      -- master (AP) mode: beacon an OPEN network on ch (default 1) (#216)
nab.wifi_send(dst_mac, data)  -- raw data frame at the 802.3 payload seam; dst_mac = 6-byte string
nab.wifi_recv([timeout_ms])   -- -> src_mac, payload | nil; bounded main-loop RX buffer
nab.config()                  -- -> {ssid=,psk=,url=} persisted in the config sector, or nil
nab.config{ssid=,psk=,url=}   -- persist (survives power cycles); true = written+verified,
                              --   false = flash already held this record (write skipped)
```

HW-verified (M5/M8): `nab.led` lights each named LED, `nab.button()` tracks the physical
button, `nab.beep()` is audible. Caveats:

- **`nab.beep` bypasses `SCI_VOLUME`** (the sine test is a fixed-level diagnostic tone), so
  `nab.volume` has no effect on it. `nab.beep`'s `ms` is a rough CPU busy-loop, but `nab.delay`
  (#102) is timed off the 1 ms System Timer (`counter_timer`) - the same clock the LED fades use.
  The timer is calibrated to the **16 MHz ring oscillator** (firmwareV2 never calls `init_pll()`),
  so `TMRLR=0xFC18`, not `mtl/firmware`'s 32 MHz `0xF830`; the ring osc isn't crystal-accurate, so
  `ms` is nominal.
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
- **`nab.config` (#214) built, not HW-confirmed** - the simulator models flash *reads* only,
  so the write path (V1's `write_uc_flash_sec` port in `hal/config.c`, erase+program of the
  last 4 KB internal-flash sector from a `.ramfunc`) needs the rig: write creds from the
  REPL, power-cycle, read them back. The writer takes no address - the sector base is a
  compile-time constant, so it cannot touch the firmware below `0x1F000` - but the
  full-backup rule above still applies before the first live write. A write masks IRQs for
  ~63 ms (flash supplies no code/data while programming itself), so expect a wifi/tick hiccup.
- **`nab.rfid()` (#117) built + sim-verified, not HW-confirmed.** A documented chip isn't a
  *responding* chip until probed (see the M6 AT45 lesson) - run `rfidprobe` first. Reliability
  fix (#180): `hal/rfid.c` now tracks field state and drops/raises the RF field once instead of
  per-poll (passive tags were power-cycled every call). #195 closed the two follow-ups: the
  settle delay is the tick-calibrated `DelayMs(1)` (V1's value), and the event-shaped path is
  `nab.on('rfid', fn)` - C scans every ~750 ms and calls back only on UID transitions (tag
  arrive/change -> `fn(uid)`, two consecutive empty scans -> `fn(nil)`).
- **`nab.on`/`nab.wait` (#195) built, not HW-confirmed** (the sim models neither timer nor
  RFID, so callbacks can't fire there). Events dispatch only from the cooperative pump: the
  REPL's between-lines idle loop (RFID scans additionally gated on ~500 ms of console quiet,
  protecting the 16-byte RX FIFO from a mid-#LC-frame scan stall) and `nab.wait`. A callback
  error prints and returns to the loop (`lua_pcall`, principle 3). The boot chunk's `watch()`
  is the demo: `run()`'s tag reactions, event-shaped, with the prompt still usable.

### SPI0 RX-FIFO + DREQ gotcha
`WriteSPI` (SPI0) clocks in a byte per write but never consumes it, so a run of SCI writes
fills the RX FIFO and shifts later `vlsi_read_sci` results - `audio.c` **drains the RX FIFO at
the end of every `vlsi_write_sci`**. DREQ also dips right after those writes, so the SDI
control feed **waits for DREQ** (bounded) per byte rather than aborting on DREQ-low. Both were
the root cause of an initially-silent `nab.beep`.
