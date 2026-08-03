# lua/firmware — Lua on the Nabaztag:tag (bare metal, JTAG-only)

An alternative **Layer 0**: a bare-metal **PUC-Rio Lua 5.4** runtime on the stock
hardware, replacing the mtl track's C-bytecode VM. Not eLua (dormant, Lua 5.1, its
board layer duplicates our HAL, and its RAM trick is moot given 1 MB ExtRAM). Tracking
issue: [#87](https://github.com/rngtng/NabaztagHackKit/issues/87); roadmap lives in
GitHub Issues, not here.

## Design principles

Five principles ([#183](https://github.com/rngtng/NabaztagHackKit/issues/183), from
embedded-Lua practice — ArduPilot, rusEFI, Lua-RTOS). **Binding on new work: a change
that breaks one needs a stated reason.**

1. **Layered API — HAL in C, behaviour in Lua, narrow seam.** *(established)* RFID, ear
   motors, LED PWM, audio stay in C (`src/hal/`); Lua sees only the [`nab` module](#the-nab-module).
   New hardware gets a C driver + a thin `nab.*` binding, never a register poke or timing
   loop in Lua. Behaviour on top of that seam lives in [`../lib/`](../lib/), in Lua.
2. **Cooperative event core — never Lua in an ISR.** *(established, #195)* `src/utils/event.c`:
   C pollers (debounced button, ~750 ms RFID scan) post edge events into a small fixed queue;
   Lua drains it via `lua_pcall`'d `nab.on` callbacks, from the REPL's idle loop or `nab.wait()`.
   No interrupt handler ever calls into Lua — the 1 ms tick ISR only counts and steps fades.
   **The reactor (#283)** closes the gap this left: draining events is not enough if a
   blocking call means nothing gets drained. `nab.on("tick", fn)` hands Lua a slice on every
   pump iteration, and the resident `sched` ([`../boot/boot.lua`](../boot/boot.lua)) builds two
   entry points on it — `sched.pump(fn)` for the pull-style state machines that already exist
   (`hw.ears` `:step()`, `net.iface` `:poll()`), `sched.spawn`/`sched.sleep` for sequential
   behaviour written top-to-bottom as a coroutine. `nab.delay` is now an alias of `nab.wait`:
   a delay that silently disabled a script's own callbacks was the bug, not a second primitive
   worth keeping. **New blocking bindings must pump** — a call that spins without advancing the
   reactor reopens exactly the hole `firmware:test:sched` exists to catch.
3. **Explicit memory + error budget.** *(partly established)* Flash is tracked to the byte
   (124 KB, `-Werror`, parser-less image). Every chunk runs under `lua_pcall`, so a script
   fault returns to the prompt instead of crashing the rabbit. *Target:* a fixed Lua-heap cap.
4. **Partial-update-friendly script structure.** *(target)* Remote loading should swap small
   `luac` payloads, not reflash. **Open gap:** script slots need versioning + rollback, and
   `LLC2_4c` has **no external flash** (#94, `CS_FLASH` unpopulated) — a slot region must come
   out of the ~9.9 KB free internal flash or the volatile 1 MB ExtRAM.
5. **Sandbox by construction.** *(partly established)* Stdlib is trimmed to `base + string +
   table + coroutine` (the reactor's, 2,300 B measured) — no `os`/`io`/`package`/`debug`/`loadlib`, `dofile`/`loadfile` removed. The
   parser-less image hardens this further *for source*: with no on-device compiler the rabbit
   cannot `eval` a typed-in program. It does **not** harden the bytecode side - dropping
   `lparser`/`llex` removed a front end that rejects malformed input by construction and left
   `lundump`, which PUC-Rio documents as unchecked ("maliciously crafted binary chunks can
   crash the interpreter"), as the whole input surface. `task lua:firmware:test:bytecode`
   measures it; the `#LC` frame carrying no integrity check is the cheap half to close. New bindings are bounded `nab.*` calls; don't re-add
   a general-purpose library without a security review.

> ⚠️ [#184](https://github.com/rngtng/NabaztagHackKit/issues/184)'s hardware list is partly
> web-sourced and **contradicts this board**: it claims LEDs via `MCP23017` (actually a
> **TLC594x over SPI**, `src/hal/led.c`) and ears via `L293D` (actually **OKI FTM PWM**,
> `src/hal/motor.c`). Trust the [teardown](../../docs/hardware-dissection.md) and a probe.

## Hardware

- MCU **OKI ML67Q4051**, ARM7TDMI (no FPU, no Thumb-2, vectors at `0x0`). `init.s` runs
  `init_pll()` before `main` (#269), so **every** image — product and examples — runs at
  **32 MHz** off the 8 MHz crystal.
- Internal flash `0x08000000`, **124 KB** usable (last 4 KB sector = `nab.config`).
- Internal RAM `0x10000000`, 16 KB (too small for a Lua state). External RAM `0xD0000000`,
  **1 MB** — the Lua heap (`_sbrk`), less the top 32 KB reserved for the USB allocator.
- Console + debug: 8-pin JTAG, and **UART0 on PB0(TX)/PB1(RX) at 115200 8N1**, bidirectional
  (#203/#207/#271). One divisor serves all images because of the PLL above.
- Board revision `LLC2_4c` (`inc/common.h`); pin diffs for the other two:
  [PCB revisions](../../docs/hardware-dissection.md#pcb-revisions-pcb_release).

## Build, simulate, flash

Host needs only Docker + Task. Two target kinds: the **product firmware** (`src/main.c`, the
Lua host — the default) and standalone **examples** (`examples/<name>.c`, one peripheral each,
own `main()`, no Lua).

```sh
task lua:firmware:build                  # -> bin/firmware.{elf,hex,bin,sim}
task lua:firmware:build EXAMPLE=blink    # -> bin/blink.{elf,hex,bin}
task lua:firmware:simulate               # run the product in the Unicorn sim, no hardware
task lua:apps:simulate APP=apps/led-demo.lua ARGS=--leds   # feed a Lua app, live LED view
task lua:firmware:simulate:repl          # interactive REPL against the sim
task lua:firmware:flash                  # JTAG flash via the Raspberry Pi bridge
task lua:firmware:flash:repl             # flash, then drive the REPL over UART
task lua:verify                          # definition of done for this track
```

`arm-none-eabi-gcc` + newlib-nano, `-mcpu=arm7tdmi -mthumb -mthumb-interwork`, our own startup
(`-nostartfiles`) and `--gc-sections`. Our sources are **`-Werror`**-clean under `-Wall -Wextra
-Wpedantic -Wpointer-arith -Wcast-align` (`-Wcast-align` matters on ARM7TDMI: an unaligned
32-bit load rotates silently instead of faulting). Vendored Lua and the vendored usb/net dirs
are exempt — see the `Makefile`, which also documents why those dirs must stay at `-Os`.

Detail lives with the tool that owns it: **simulator model + injection protocol** in
[`../tools/simulator/README.md`](../tools/simulator/README.md), **off-device `luac` +
`#LC` framing** in [`../tools/luac/README.md`](../tools/luac/README.md), **JTAG/Pi rig +
UART console** in [`../tools/openocd/README.md`](../tools/openocd/README.md).

> ⚠️ **Brick risk:** never erase or program internal flash without a verified full backup.
> IDCODE `0x3f0f0f0f` over JTAG = the CPU is alive.

## Lua runtime

PUC-Rio Lua 5.4 ([`lua/`](lua/), vendored — see [`PROVENANCE.md`](../../PROVENANCE.md)), glued
in [`src/main.c`](src/main.c). Bare metal supplies neither a console nor a heap, so `_read`/
`_write` route stdin/stdout through UART0 and `_sbrk` hands out the ExtRAM window.

Tuned to the flash budget (`luaconf.h` sets `LUA_32BITS` — 32-bit int + float, no `double`):

- **Stdlib = base + string + table + coroutine.** No `math`/`io`/`os`/`package`/`debug`/`utf8`.
  `coroutine` (2,300 B, measured) is what the #283 cooperative reactor is built on.
- **Integer math is exact; float *printing* is approximate.** `1+1`→`2`, `2^10`→`1024.0`, but
  `1/2`→`0.0` — fractional digits drop. Arithmetic is correct internally; only decimal
  rendering is stubbed (a real dtoa is future work).
- **Number I/O is newlib-free** (#106): compact decimal parser instead of `strtof`, libm-free
  `^`/`%` (fractional `^`→NaN), in-tree `snprintf`. **No float ever crosses a variadic call**
  (#213) — C argument promotion would make it a `double` and link libgcc's soft-float.
- **Parser-less by design** (#128): `lparser`/`llex`/`lcode` are dropped (~18.9 KB), so the
  rabbit runs *only* `luac` bytecode. Every REPL line and the resident boot chunk is compiled
  **off-device** by a `LUA_32BITS`-matched host `luac`. That freed budget is what lets the
  wifi C fit at all. A source line typed at a bare terminal will not run.
- Each REPL line is its own chunk, so `local`s don't persist — use globals, same as stock `lua`.

### Flash budget

`bin/firmware.elf` uses **117,620 B of 124 KB (~9.2 KB free)**. Roughly: ~23 KB the USB +
802.11/WPA2 stack, ~3.2 KB the #283 reactor (`coroutine` 2,300 B measured, the resident
`sched` chunk and the `nab.on("tick")` seam), ~2.1 KB the #234 provisioning plumbing,
~1.5 KB the #195 event core, 836 B `nab.config`, ~0.8 KB the #216 raw-frame/AP bindings,
~0.65 KB the #235 OTA writer, 560 B the #273 `nab.wifi_scan`/`nab.wifi_seen` scan bindings
(station mode only), 552 B the #265 non-blocking audio stream HAL. Everything above that
last one is Lua in [`../lib/audio/`](../lib/audio/) and costs no flash.

Two things keep it from being worse, and both are load-bearing:

- **WPA2-CCMP only** (#124, 3,896 B): HMAC-MD5, RC4 and every WEP/WPA1/TKIP path are gone.
  `nab.wifi` joins open or WPA2-PSK(AES) and rejects anything else at scan/auth.
- **[`src/utils/libc_shim.c`](src/utils/libc_shim.c)** supplies local `rand`/`srand`/
  `__assert_func`. Without it the vendored net stack's `rand()` re-links ~9 KB of newlib
  vfprintf/FILE machinery through an asserting archive member. **A new libc call that grows
  the image needs the same treatment — check the map's "Archive member" section; don't trust
  `--gc-sections` alone.**

`-Os` and Lua 5.5 are **not** levers. The two cheapest remaining ones are demo assets,
4,547 B together: `nab.tone()`'s built-in MP3 (`inc/tone_mp3.h`, 2,160 B) and the resident
boot chunk (`gen/boot_lc.h` from `../boot/boot.lua`, 2,387 B — `run`/`watch`/`ledshow` plus
two hard-coded RFID UIDs, largely duplicating [`../apps/`](../apps/)). Both are product
decisions, not refactors. `task lua:firmware:build` fails loudly on overflow.

## The `nab` module

The one seam between Lua and hardware, registered in `src/main.c`:

```lua
nab.led(name, r, g, b)        -- name: nose|belly|left|right|bottom; r/g/b 0..127 (raw, no gamma)
nab.led8(name, r, g, b)       -- same LEDs, r/g/b 0..255 through the gamma-2.2 table - instant
nab.fade(name, r, g, b, ms)   -- background fade over ms; returns immediately (#102)
nab.delay(ms)                 -- block ms, timed off the 1 ms tick (the clock fades use)
nab.time()                    -- -> ms since boot (wrapping 32-bit tick)
nab.wait(ms)                  -- sleep ~ms while running the event pump, so nab.on callbacks fire
nab.on(name, fn|nil)          -- register/clear a callback (#195): "button" -> fn(pressed) on
                              --   debounced edges; "rfid" -> fn(uid|nil) on tag arrive/leave
                              --   (registering starts the background ~750 ms scan)
nab.button()                  -- -> true while the head button is held (polled, undebounced)
nab.wheel()                   -- -> 0..255, ADC ch.2 (the back wheel - an analog pot, HW-verified:
                              --   255 at rest, smooth 255->0->255 across its travel)
nab.rfid()                    -- -> lowercase hex UID string, or nil (one live scan)
nab.ear_move(n, dir)          -- n: 1|2 (1 = left); dir "forward"|"reverse". Full speed only (#179)
nab.ear_stop(n)               -- n: 1|2
nab.ear_pos(n)                -- -> raw wrapping 16-bit encoder edge count, NOT an angle
nab.volume(v)                 -- 0 = loudest .. 254 = quietest (SCI_VOLUME)
nab.beep(freq, ms)            -- VS1003 sine test: freq = pitch byte 0..255. Bypasses SCI_VOLUME
nab.play(data)                -- stream bytes over SDI - real decoded audio, nab.volume applies.
                              -- Blocking, but pumps the reactor between feeds (#283)
nab.play_start()              -- open a non-blocking stream (#265); nothing here waits
nab.play_feed(data [, i])     -- -> bytes accepted (0..n). Short = FIFO full, NOT an error:
                              -- keep the rest and offer it again next turn (i = i + n)
nab.playing()                 -- -> true while the decoder still has a stream to decode
nab.play_stop()               -- close the stream, amplifier off
nab.tone()                    -- -> a built-in ~0.25 s 880 Hz MP3, for nab.play. MP3, not PCM
                              --   WAV - the VS1003B does not decode WAV.
nab.record(ms [, gain])       -- -> ~ms of mic audio as a complete WAV (8 kHz IMA ADPCM). Blocking.
                              --   gain: 1024 = 1x, 512 = 0.5x, 0 = AGC (default)
nab.rec_start([gain])         -- cooperative session: codec encodes into its ~2 KB FIFO, CPU free
nab.rec_read()                -- -> whole 256-byte ADPCM blocks, or nil. Returns immediately
nab.rec_stop()                -- close the session (codec back to decode mode)
nab.rec_wav(data)             -- wrap concatenated rec_read chunks as a WAV string
nab.wifi(ssid [, psk])        -- join an AP (WPA2-CCMP or open) -> true | nil, msg, reason
                              --   reason: "radio"|"notfound"|"auth"|"timeout" (#234 branches on it)
nab.wifi_ap(ssid [, ch])      -- master (AP) mode: beacon an OPEN network on ch (default 1) (#216)
nab.wifi_up()                 -- cold-boot the dongle without joining, so wifi_mac() is real (#233)
nab.wifi_mac()                -- -> our 6-byte station MAC (all-zero until the radio is up)
nab.wifi_scan([ssid])         -- probe every channel (~5 s) -> count | nil, msg; no ssid = broadcast.
                              --   Station mode only: fails once wifi_ap beacons (#273)
nab.wifi_seen()               -- -> {{ssid=,bssid=,rssi=,channel=,enc=}, ...} from the last scan,
                              --   deduped by bssid (max 32); enc 0 = open, 0x40|cipher = WPA2
nab.wifi_send(dst_mac, data)  -- raw data frame at the 802.3 payload seam; dst_mac = 6-byte string
nab.wifi_recv([timeout_ms])   -- -> src_mac, payload | nil; bounded main-loop RX buffer
nab.config()                  -- -> {ssid=,psk=,url=,fails=} from the config sector, or nil
nab.config{...}               -- persist it; true = written+verified, false = already identical
nab.flash_firmware(image)     -- whole-image OTA flash + reboot (#235). Never returns on success.
                              --   BRICK RISK - net.ota verifies the image before calling this
nab.sci(reg) / nab.sciw(r,v)  -- read/write a VS1003 SCI register (codec bring-up diagnostics)
```

Higher-level behaviour belongs in [`../lib/`](../lib/), not here — e.g. `nab.ear_pos` is a raw
edge count, and `lib/hw/ears.lua` (#263) is what turns it into homing and absolute positions.

`nab.record`'s RIFF header is **byte-identical to the mtl stack's** (`mtl/lib/hw/reclib.mtl`),
so anything that accepts a V1 recording accepts this one. `nab.record` is blocking; the
`rec_*` session API is the non-blocking form — the codec encodes into its own ~2 KB FIFO
(~half a second at 8 kHz; overflow drops audio but never crashes) while your script does
other work:

```lua
chunks = {}
nab.rec_start()
while nab.button() do                        -- record while held
  local c = nab.rec_read()
  if c then chunks[#chunks + 1] = c end
  nab.led('nose', 127, 0, 0)                 -- ...LEDs/ears/net between polls
end
nab.rec_stop()
nab.play(nab.rec_wav(table.concat(chunks)))
```

### What is confirmed on hardware

`LLC2_4c`, the only board revision tested. Treat everything else as *needs a probe* — a
documented chip is not a *responding* chip until you have seen it answer (the M6 AT45 lesson:
`CS_FLASH` is unpopulated, so #94 was reverted outright).

| Confirmed on hardware | Built, **not** HW-confirmed |
|---|---|
| LEDs by name, head button, ear motors + encoders (full speed) | `nab.rfid` — run `rfidprobe` first (#117) |
| `nab.beep` audible; VS1003B on SPI0 | `nab.config` write path — write creds, power-cycle, read back (#214) |
| `nab.wheel` — analog pot on ADC ch.2, 255 rest -> 0 full sweep (#123) | `nab.on`/`nab.wait` — register `watch()`, place a tag, press the button (#195) |
| `nab.play`/`nab.tone`/`nab.volume` — audible, attenuates (#123); SCI_VOLUME holds exactly as written through the #265 streaming path too, re-verified post-#283 rebase | |
| **Streaming playback + ear/net concurrency (#123/#265/#283)** — `nab.play_feed` accepts bytes end to end, plays audibly while `nab.ear_move` spins an ear (no stall either side) and separately while streaming an HTTP GET body over wifi (`lib/net` + `audio.stream`, 25 s clip, mic-confirmed audible, LED chase animating throughout - 184 frames, 28.4 s, twice reproduced) | |
| UART0 console both directions @115200 | |
| USB host + RT2501 join, WPA2-CCMP | |
| 32 MHz PLL clock (#269) | `nab.record` — sim returns a header-only WAV; blocked on #275 (#116) |
| LED fade engine animates in sim | `nab.fade` timing on real hardware (#102) |

Two #123 probe results with no code behind them: the wheel's end-of-travel **click has no
separate GPIO** (only PD2, the wheel's own ADC line, moves), and the **audio-out jack is a
mechanical normalled switch** — inserting a plug cuts the speaker without changing any GPIO.
`nab.play`/`nab.tone`/`nab.wheel` stay unrunnable in-sim (DREQ and the ADC bit are unmodeled).

## Hardware gotchas

Each of these cost real debugging time. They are not obvious from the datasheet.

- **XD16-31 must be muxed to GPIO (#275).** `BWC=0xA0` runs ExtRAM as a 16-bit bank, so the
  upper external-data-bus half carries no data — but left on its default bus function it
  toggles on every EMC *write*, and it shares package pins with the audio-control group
  (`RST_AUDIO = PIO11.7`). Every Lua-heap write burst was hardware-resetting the VS1003.
  `init_hw()` sets `PORTSEL4` bit 6 to stop it. Reads leave the bus hi-Z, which is why
  playback mostly survived and recording did not. Isolated by `examples/recprobe.c`.
- **SPI0 RX-FIFO + DREQ.** `WriteSPI` clocks in a byte per write but never consumes it, so a
  run of SCI writes fills the RX FIFO and shifts later `vlsi_read_sci` results — `audio.c`
  drains the FIFO at the end of every `vlsi_write_sci`. DREQ also dips right after those
  writes, so the SDI feed waits (bounded) for DREQ per byte rather than aborting on DREQ-low.
  Together these were the root cause of an initially-silent `nab.beep`.
- **The tick reload and the PLL move together.** `TMRLR=0xF830` is 1 ms *at 32 MHz*. It was
  patched twice (`0xFC18`, `0xFE0C`) while the chip still booted on the ring oscillator;
  #269 fixed it at the source instead. Change one without the other and every timing on the
  board — fades, `nab.delay`, the USB stack's 200 ms cadence — silently skews.
- **`init_ears` needs the `PORTSEL3` pin-mux** (`0x05550000`) to route PF0-PF5 to FTM.
  Without it the PWM pins stay GPIO and the motors are silent.
- **A config write masks IRQs for ~63 ms** (flash supplies no code or data while programming
  itself), so expect a wifi/tick hiccup. The writer takes no address — the sector base is a
  compile-time constant, so it physically cannot touch the firmware below `0x1F000`.
- **Ear motors move noticeably less per second than #179's baseline, on this rig, today** -
  `examples/earprobe.c`'s `sweep_motor()` (the same tool #179 used) now measures encoder
  delta 2-3 over ~1.5 s at full duty, where #179 recorded 8-11 under the same test. Two
  likely-innocent explanations were checked and ruled out: the #269 32 MHz PLL migration
  (2026-07-26, after #179's 2026-07-12/17 characterization) never touched `motor.c`, so
  suspected the FTM PWM frequency (APB_CLOCK-derived, "488 Hz @ 32 MHz" per the source
  comment) had silently quadrupled uncorrected - but `mtl/firmware/src/hal/motor.c` runs
  the *identical* `FTM2CON`/`MOTOR_SPEED_CONTROL` config at the same 32 MHz on real
  hardware, and `run_motor()` is byte-identical between the two tracks (diff-verified,
  modulo CRLF). Neither a clock-migration regression nor a lua-port bug explains it.
  Leading remaining explanation: physical rig state (motor wear, disassembled-cover
  friction, or a weaker bench supply than #179's session) - not firmware-fixable, and
  needs a physical inspection or an mtl-side `earprobe`-equivalent run on this same rig
  to confirm, not more source archaeology.
- **`nab.play`/`nab.tone` play far quieter than `nab.beep`'s built-in sine test at the same
  volume setting (#123) — objectively measured, not just by ear.** A close-range mic capture
  (MacBook mic, `ffmpeg`/`avfoundation`) put `nab.beep` around -20 dBFS peak; the decoded
  `nab.tone()` stayed below -40 dBFS throughout its playback window in every take — a 20+ dB
  gap, sometimes not registering above the room-noise floor at all. Frequency isn't it (880 Hz
  and 1760 Hz both quiet vs. beep). **FW1's `patchwma` is ruled out as the cause**: ported as
  `vlsi_patch()` (`init_vlsi()`, `hal/audio.c` — ten `WRAM_ADDR`/`WRAM` writes loading a VLSI
  microcode patch, the last unported config difference between the two tracks), then A/B'd
  twice on hardware - once live mid-session (no clear difference by ear) and once by disabling
  it at boot and re-measuring by mic (same 20+ dB gap either way). Kept the patch in regardless
  (official VLSI fix, harmless, closes a real config gap between the tracks) - but it is
  confirmed **not** the loudness lever. **The gap itself is real, quantified, and still
  unexplained** - the decode path's actual output gain-staging vs. the sine test (which may
  bypass normal output scaling entirely) needs a dedicated VS1003 datasheet dive to pin down,
  out of scope for #123's volume-attenuation DoD (which is unaffected - `nab.volume` still
  correctly attenuates whatever level `nab.play` does produce).

### LED map (verified with the `ledmap` probe)

| Channel | Physical | | Channel | Physical |
|---|---|---|---|---|
| `LED_RGB_1` | belly (upper) | | `LED_RGB_4` | belly right |
| `LED_RGB_2` | belly bottom | | `LED_RGB_5` | **nose** |
| `LED_RGB_3` | belly left | | | |

`nab.led` uses this raw map; `nab.led8`/`nab.fade` go through `led.c`'s `convled[]` logical
remap. `main.c` carries one `led_names[]`/`led_sel[]`/`led_logical[]` row per LED so all three
bindings land on the same physical LED. `led.c` itself is re-synced from `mtl/firmware` (#45):
gamma-2.2 with no low-end dead zone, and a fade engine stepped from the 1 ms timer ISR — which
is why led.c's main-context writers mask that IRQ around their SPI flush.

## Layout

```
sys/                ARM7TDMI startup, linker script, OKI register defs (copied from mtl/firmware)
  asm/init.s        reset vector, clock + EMC init, init_pll, stacks, .data/.bss -> main()
  src/tick.c        1 ms system tick - counter_timer + DelayMs; steps the LED fades
  src/irq.c         IRQ handler table + init
src/main.c          product entry: boots Lua 5.4 into a bytecode REPL; all nab.* bindings
src/hal/            one file per peripheral: spi, led, button, audio (VS1003), adc, i2c,
                    rfid (CRX14), motor (ears), uart, config (flash sector), ota, wifi
src/net/            802.11 + WPA2: ieee80211, eapol, aes128, hash  (vendored, -Os)
src/usb/            ML60842 OHCI host stack + RT2501 driver (#143)  (vendored, -Os)
src/utils/          event.c (cooperative event core), fmt.c (number/printf shims), libc_shim.c
lua/                vendored PUC-Rio Lua 5.4; the Makefile compiles a subset
gen/boot_lc.h       generated: ../boot/boot.lua baked to bytecode by tools/luac/embed.py
examples/*.c        standalone bring-up progs, one per binary (EXAMPLE=); *probe.c per peripheral
test/host/          host-side C unit tests under ASan/UBSan (task lua:firmware:test:host)
test/bytecode/      malformed-bytecode robustness of the loader (task lua:firmware:test:bytecode)
test/*.expected     golden transcripts for the bytecode + injection tests
```

`sys/`, `inc/common.h` and `hal/` are **copied** from `mtl/firmware` — a register fix there may
apply here, so grep the sibling before assuming a divergence is intentional. Otherwise this
layer is self-contained. Vendoring origins: [`PROVENANCE.md`](../../PROVENANCE.md).
