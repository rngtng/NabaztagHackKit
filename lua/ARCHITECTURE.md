# lua track — architecture & dependency map

The cross-layer view of the lua track: what runs on silicon, what the C firmware
holds, where the one seam is, what is Lua, and **which parts depend on which**.

Each layer README already owns its own rationale and is the authority on it —
[`firmware/README.md`](firmware/README.md) (design principles, flash budget, the
`nab` module reference, hardware gotchas), [`lib/README.md`](lib/README.md) and
the per-lib READMEs, [`boot/README.md`](boot/README.md), and one README per tool
([simulator](tools/simulator/README.md), [luac](tools/luac/README.md),
[openocd](tools/openocd/README.md)). This page deliberately does
**not** restate them. It exists because no single layer README can hold the
thing a reader most often needs: the edge list — including the edges that leave
this track entirely.

Sizes below are the committed measurements from those READMEs and from the
build's own size report. They are re-derivable, not authoritative here: run
`task lua:firmware:build` (flash) and `task lua:lib:size` (bytecode) rather than
trusting this page.

## The stack in one picture

```
                                      apps/*.lua            ← demo apps (RAM)
                                      lib/{net,hw,sys,audio} ← behaviour (RAM, ~50 KB .lc)
   Lua                                boot/boot.lua          ← sched, resident (flash)
   ───────────────────────────────────────────────────────────────────────────
   the seam                           nab.*  38 names / 37 C functions
   ───────────────────────────────────────────────────────────────────────────
                    src/main.c        Lua host: state, trimmed stdlib, #LC REPL,
                                      all bindings, event dispatch, _sbrk/_read/_write
   C               src/utils/         event core, fmt/number shims, #LC header, libc shim
                   src/hal/           spi led button audio adc i2c rfid motor uart
                                      wifi config ota          ← 12 drivers
                   src/net/  src/usb/ 802.11 + WPA2 · OHCI + RT2501   (vendored, -Os)
                   lua/               PUC-Rio Lua 5.4.7, parser removed (vendored)
                   sys/               init.s, tick, irq, linker script, OKI regs
   ───────────────────────────────────────────────────────────────────────────
   hardware        OKI ML67Q4051 (ARM7TDMI @32 MHz) + 1 MB ExtRAM + peripherals
```

Two properties make this a stack rather than a pile, and both are checkable:

- **Nothing below `main.c` knows Lua exists.** `lua_State` appears in exactly
  one TU (`src/main.c`); `src/utils/fmt.c` includes `lua.h` only for
  `LUA_NUMBER`. Every driver is callable — and host-testable — without a Lua
  runtime, which is what `firmware/test/host/` and `examples/` both rely on.
- **There is no C TCP/IP.** The C side stops at the 802.11 link layer; ARP
  through HTTP is Lua in [`lib/net/`](lib/net/README.md).

## 1. Hardware

Board `LLC2_4c`; teardown and PCB revisions in
[`../docs/hardware-dissection.md`](../docs/hardware-dissection.md).

| Part | Bus / pins | Driver | Reaches Lua as |
|---|---|---|---|
| OKI ML67Q4051, ARM7TDMI | — | `sys/asm/init.s`, `sys/src/{tick,irq,clock}.c` | `nab.time`, all timing |
| Internal flash 124 KB + 4 KB config sector | — | `hal/config.c`, `hal/ota.c` | `nab.config`, `nab.flash_firmware` |
| External RAM 1 MB @ `0xD0000000` | EMC, 16-bit | `_sbrk` in `main.c` | the Lua heap |
| TLC594x LED driver, 5 RGB LEDs | SPI1 | `hal/led.c` | `nab.led`, `nab.led8`, `nab.fade` |
| VS1003B codec (speaker + mic) | SPI0 + DREQ | `hal/audio.c` | `nab.play*`, `nab.rec*`, `nab.volume`, `nab.beep`, `nab.sci*` |
| CRX14 RFID coupler | I²C | `hal/rfid.c` over `hal/i2c.c` | `nab.rfid`, `nab.on("rfid")` |
| Ear gearmotors + hole encoders | OKI FTM PWM + counters | `hal/motor.c` | `nab.ear_move/stop/pos` |
| Head button | GPIO PI3 | `hal/button.c` | `nab.button`, `nab.on("button")` |
| Back wheel (analog pot) | ADC ch.2 (PD2) | `hal/adc.c` | `nab.wheel` |
| RT2501/RT2573 USB WiFi dongle | ML60842 OHCI host | `hal/wifi.c` over `src/usb/` + `src/net/` | `nab.wifi*` |
| Console | UART0 PB0/PB1 @115200 | `hal/uart.c` | stdin/stdout, the `#LC` REPL |
| Debug | 8-pin JTAG | `tools/openocd/` | flashing only |

Not populated / not present: `CS_FLASH` (no external flash, #94), no separate
GPIO for the wheel's end-of-travel click, no GPIO for the audio-jack switch.
That absence is what leaves principle 4 (partial updates) without a home.

## 2. The HAL — `firmware/src/hal/` + `firmware/sys/`

12 drivers, 3,119 lines including headers. The API is deliberately narrow and
register-shaped: `init_x()` plus a handful of verbs, no state machines, no
policy, **no `lua_State`**.

| Driver | Public API width | Notes |
|---|---|---|
| `spi.c` | 5 fns | two buses; **byte-identical to `mtl/firmware`** |
| `led.c` | 5 fns | gamma-2.2 + fade engine stepped from the 1 ms ISR |
| `audio.c` | 14 fns | the widest: SCI, blocking play, stream session, record session |
| `wifi.c` | 12 fns | the fattest dependency: wraps `src/usb/` + `src/net/` |
| `rfid.c` | 10 fns | CRX14 command set; `rfid_read_uid` is the only one Lua reaches |
| `motor.c` | 5 fns | raw wrapping edge count, no homing (that is `lib/hw/ears.lua`) |
| `i2c.c` | 3 fns | `rfid.c`'s only bus dependency — which is why both host-test |
| `uart.c` | 5 fns | polled both ways; `main.c`'s `_read`/`_write` sit on it |
| `config.c` | 2 fns | bounded to the last 4 KB sector — takes no address, by design |
| `ota.c` | 1 fn | whole-image writer; sectors 0..N below config |
| `button.c` | 2 fns | raw level; debouncing is `utils/event.c`'s job |
| `adc.c` | 2 fns | one channel is the whole driver |

`sys/` is the layer beneath: reset vector, PLL, EMC init, stacks, the 1 ms tick,
the IRQ table, the linker script, the OKI register headers.

**Two edges here are worth knowing about**, because neither points the way the
diagram suggests:

- `sys/src/tick.c` → `hal/led.h`: the 1 ms ISR calls `led_fade_tick()`. The
  bottom layer depends on a driver, and `led.c` masks that IRQ around its own
  SPI flush in return. It works, and it is documented at both ends, but the two
  files are coupled in a loop.
- `src/utils/event.c` → `hal/button.h`, `hal/rfid.h`: the event core sits
  *above* the HAL, not beside it. That is why it host-tests cleanly (its
  dependencies are two functions) and why `button.c` deliberately holds no
  debouncing.

## 3. What else the C firmware holds

| Area | Files | Lines | Origin |
|---|---:|---:|---|
| `lua/` — PUC-Rio Lua 5.4.7 | 61 | 30,280 | vendored, 4 local edits |
| `src/usb/` — OHCI host + RT2501 | 19 | 5,522 | vendored from mtl/V1 |
| `src/net/` — 802.11, EAPOL, AES-128, hashes | 8 | 4,203 | vendored from mtl/V1 |
| `src/hal/` | 24 | 3,119 | ported from `mtl/firmware`, then diverged |
| `examples/` — one-peripheral bring-up progs | 19 | 3,222 | original |
| `sys/` — startup, tick, irq, linker, regs | 11 | 2,431 | copied from `mtl/firmware` |
| `src/utils/` — event, fmt, lcframe, libc shim | 11 | 1,550 | original |
| `src/main.c` — the Lua host | 1 | 1,444 | original |

Of the vendored Lua tree the build compiles a **subset**: 16 core files (of 19
— `lcode`/`llex`/`lparser` are dropped, ~18.9 KB) plus `lauxlib`, `lbaselib`,
`lstrlib`, `ltablib`, `lcorolib`. `ldump` goes too — the device loads bytecode
and never writes it. The two remaining references into the dropped parser
(`f_parser` in `ldo.c`, `luaX_init` in `lstate.c`) are guarded by
`-DLUA_NOPARSER`, which is one of the **four local edits** that a Lua upgrade
must re-apply (the others: `luaconf.h`'s `LUA_32BITS` + off-newlib number and
console hooks, and `lbaselib.c` dropping `dofile`/`loadfile`).

`src/utils/` is small but load-bearing, and two of its four files are global
in effect:

- **`fmt.c`** overrides `snprintf`/`vsnprintf` for *every* target and supplies
  Lua's `luai_*` number hooks — which is why `-Ilua` is on the include path even
  for examples that never link Lua.
- **`libc_shim.c`** supplies local `rand`/`srand`/`__assert_func`. Without it
  the vendored net stack's `rand()` re-links ~9 KB of newlib `vfprintf`/FILE
  machinery. Any new libc call needs the same audit — via the map's *Archive
  member* section, not `--gc-sections`.

### Flash budget

`bin/firmware.elf` = **119,332 B of 126,976 B**, ~7.5 KB free. Roughly: ~23 KB
USB + 802.11/WPA2, ~3.2 KB the reactor (`coroutine` 2,300 B measured), ~2.1 KB
provisioning plumbing, ~1.5 KB the event core, 2,160 B the `nab.tone()` MP3,
836 B `nab.config`, ~0.8 KB the raw-frame/AP bindings, ~0.65 KB the OTA writer,
560 B the scan bindings, 552 B the stream HAL, plus the resident boot chunk.
Full breakdown and the two levers that keep it from being worse:
[`firmware/README.md`](firmware/README.md).

## 4. The seam — `nab.*`

One table, registered in `main.c`: **38 names, 37 C functions** (`nab.delay` is
an alias of `nab.wait`). Full signatures live in
[`firmware/README.md`](firmware/README.md); what matters structurally is the
shape of the mapping.

| Group | Names | Depends on |
|---|---:|---|
| LEDs | 3 | `hal/led.c` |
| Audio out | 8 | `hal/audio.c` |
| Audio in | 5 | `hal/audio.c` |
| WiFi | 8 | `hal/wifi.c` → `src/usb/` + `src/net/` |
| RFID / button / wheel / ears | 7 | `hal/{rfid,button,adc,motor}.c` |
| Time + events | 4 | `utils/event.c`, `sys/src/tick.c` |
| Persistence | 2 | `hal/{config,ota}.c` |
| Codec diagnostics | 2 | `hal/audio.c` |

Almost every binding is a thin argument check over exactly one HAL call. Four
are not, and they are where the interesting behaviour lives: `nab.wait` (pumps),
`nab.play` (pumps between feeds), `nab.wifi_recv` (pumps while waiting),
`nab.on` (registers into the registry table `dispatch_events` reads).

## 5. The Lua standard library on the device

Opened in `main.c`: **`base` + `string` + `table` + `coroutine`**, plus `nab`.

Dropped: `math`, `io`, `os`, `package`, `debug`, `utf8`, `loadlib`, and from
`base` also `dofile`/`loadfile`. The consequences are structural, not cosmetic:

- **No `require`, no `package.path`, no module system.** Every lib module is a
  chunk that extends a global table (`net.*`, `sys.*`, `hw.*`, `audio.*`,
  `sched`). Load order is a human responsibility (see §6).
- **No `math`.** Integer division `//`, `%` and the bitwise operators are
  language syntax and survive; `math.floor`/`abs`/`min` do not exist. The libs
  are written accordingly.
- **No `os`.** `os.time`/`os.date` are absent, which is the entire reason
  [`lib/sys/time.lua`](lib/sys/README.md) exists.
- **`load` stays** — and with the parser gone it accepts only bytecode. That
  makes `lundump` the whole input surface, which is what
  `task lua:firmware:test:bytecode` exists to measure.
- **Numbers are 32-bit** (`LUA_32BITS`: 4-byte int, 4-byte float). This is not
  a detail — it is why the host `luac` must be built from this same vendored
  tree, and why the host tests run under that image's `lua`.

## 6. The custom Lua layer

| Where | What | Lives in | Size (stripped `.lc`) |
|---|---|---|---|
| `boot/boot.lua` | `sched` — the cooperative reactor — + demo helpers | **flash** | 2,387 B |
| `lib/net/` | link, arp, ipv4, udp, dns, dhcp, tcp, http, iface, setup, provision, ota | RAM | 35,585 B |
| `lib/audio/` | player, stream, midi, volume | RAM | 6,763 B |
| `lib/sys/` | ntp, time | RAM | 4,702 B |
| `lib/hw/` | ears | RAM | 3,424 B |
| `apps/` | 10 demo apps | RAM | — |

**~50.5 KB of bytecode across 19 modules**, against ~7.5 KB of free flash.

`sched` is the only Lua that ships inside the image, and it is a runtime
service rather than a convenience: `nab.on("tick", fn)` is called by
`dispatch_events` after the C event queue drains, so `sched.tick` runs from the
REPL's idle loop *and* from inside every binding that pumps. It also **wraps
`nab.on`** so an app registering `"tick"` cannot displace the reactor.

Everything in `lib/` is pull-style by convention: methods return work to do, the
caller owns the clock (`nab.time`), and the HAL is injected — which is exactly
what makes all of it unit-testable off-device under `task lua:lib:test`.

## 7. The dependency graph

### Inside the C firmware

```
main.c ──► hal/* ──► sys/inc (registers)         hal/wifi.c ──► usb/* ──► net/*
   │  └──► utils/{event,fmt,lcframe}                              ▲
   └──► lua/ (vendored)                          net/eapol.c ─────┘ and ──► hal/led.h  ⚠
utils/event.c ──► hal/{button,rfid}              sys/src/tick.c ──► hal/led.h  ⚠
```

The two ⚠ edges point the wrong way and are both real:

- `net/eapol.c`'s PBKDF2 loop (`F()`) calls `set_led(1..3)` as a progress
  indicator and pumps `usbhost_events()` + drains RX from inside the key
  derivation. So a WPA2 join **writes the LEDs behind the app's back**,
  bypassing the `nab` seam entirely. It is vendored code and it predates the
  seam, but it is reachable from `nab.wifi(ssid, psk)` on every join.
- `sys/src/tick.c` → `hal/led.h`, discussed in §2.

### Across the seam

Lua depends on C only through `nab.*`. C depends on Lua only in `main.c`. The
one thing that crosses in the other direction is the **resident boot chunk**:
`firmware/gen/boot_lc.h` is generated from `boot/boot.lua` at build time, so the
firmware build has a hard dependency on the `tools/luac` Docker image.

### Between Lua libs

```
apps/*            ──► lib/* , sched , nab
lib/audio/stream  ──► net.http , net.tcp            ← the one cross-lib edge
lib/audio/player  ──► sched , nab
lib/net/iface     ──► arp ipv4 udp dhcp dns tcp http link , sys.ntp (soft) , nab
lib/net/setup     ──► http iface link ota , nab      lib/net/provision ──► setup , nab
lib/sys/time      ──► nab.time            lib/sys/ntp ──► (nothing — pure string.unpack)
lib/hw/ears       ──► sched , nab
```

`lib/net/link.lua` and `lib/sys/ntp.lua` are the only leaves. `iface.lua` (420
lines) is the hub — it is what every blocking flow (`:dhcp`, `:resolve`,
`:http_get`, `:ntp`) hangs off, and the module the other libs reach through.
"Four independent libs" is not accurate: `audio` needs `net`, and `net` softly
needs `sys`.

### Out of the track entirely

`lua/firmware` shares source with `mtl/firmware`. Ten files are **byte-identical
twins** today — a fix in one is a silent bug in the other:

```
src/hal/spi.c   src/net/aes128.c   sys/inc/ml674061.h   sys/inc/ml60842.h
src/usb/{hcd,hcdmem,list,rt2501usb_buffer,usbctrl,usbhcore}.c
```

Everything else has diverged, some far: `audio.c` (621 changed lines),
`ieee80211.c` (536), `eapol.c` (395), `led.c` (349), `rfid.c` (341), `uart.c`
(306), `hash.c` (296), `i2c.c` (115), `motor.c` (61), `rt2501usb.c` (26). Some
divergences are deliberate and must **not** be reconciled — see the TX
frame-ownership note in [`../PROVENANCE.md`](../PROVENANCE.md). Nothing in the
build or the test suite distinguishes the two cases.

### Build and tooling

```
task lua:firmware:build ──► tools/luac image ──► boot/boot.lua ──► gen/boot_lc.h
                       └──► firmware Docker image (arm-none-eabi-gcc)
task lua:apps:simulate  ──► firmware:build + tools/luac + tools/simulator
task lua:lib:test       ──► tools/luac image (host lua, same luaconf.h)
task lua:firmware:flash ──► Raspberry Pi JTAG rig (openocd) — hardware only
```

The `tools/luac` image is the single most depended-on tool in the track: the
firmware build, every lib and boot test, every simulator run and every hardware
REPL line all go through it. Its `Dockerfile` builds `luac` from the vendored
`firmware/lua/` tree with the device `luaconf.h`, so the two cannot drift.

## 8. What these dependencies cost

Ranked by how much they constrain the next change.

**1. Blocking HAL calls cannot pump the reactor — by construction.** The HAL
takes no `lua_State`, which is what keeps it testable and example-linkable. The
price: any HAL function that owns a multi-second loop internally freezes all of
Lua. `nab.wifi` (up to 30 s), `nab.wifi_up` (~10 s cold boot), `nab.wifi_scan`
(~5 s), `nab.record` (up to 30 s) and `nab.beep` do exactly that — during a
WPA2 join no ear steps, no player is fed and no `sched` task runs. Only
`nab.wait`, `nab.play` and `nab.wifi_recv` pump. The design principle already
says new blocking bindings must pump; these predate it. The fix pattern also
already exists twice — `play_start`/`play_feed`/`playing` and
`rec_start`/`rec_read`/`rec_stop` are what a chunked HAL looks like. WiFi has no
step-form, so `nab.wifi` cannot be made cooperative without one.

**2. The Lua userland has no delivery mechanism.** ~50.5 KB of bytecode in 19
modules, no `require`, no manifest, no bundler; `SCRIPT=`/`replpipe.py` take a
single file. The load order exists in exactly two places, neither of them
shippable: prose in the lib READMEs, and a hard-coded `MODULES` array in each
`test/run.lua`. And the console is paced for flow control at 3 ms/byte plus
80 ms/line (`tools/openocd/uart_repl.py` defaults, ~333 B/s against a 115200
line) — so shipping the boot-critical `net` subset (23 KB → ~46 KB of hex) is
minutes, and the whole lib set is longer. Freezing a subset into flash is the
known plan; the ~7.5 KB free is the problem.

**3. The cross-track twin dependency is unguarded.** Ten byte-identical files,
and the correct action on a change differs per file — copy for most, explicitly
*don't* for the frame-ownership divergence. Today the only thing standing
between those two outcomes is a reader remembering to grep the sibling. A
checksum-comparison gate over the known-twin list would turn that into a
failing task, and would have to be paired with an explicit
allowed-divergence list.

**4. `main.c` is 1,444 lines that nothing can link.** It carries `main()`, so no
host test can reach it. The repo's own rule sends new non-wiring logic to its
own TU, and `utils/lcframe.c` shows the pattern working. Still inside `main.c`
with a rule to them and no unit test: the WAV/RIFF header assembly (which
carries a byte-identical-to-mtl contract), the hex-frame reader and
`drop_lc_payload` resync path, and `dispatch_events`' ordering guarantees. They
are covered end-to-end by the sim goldens (`test:bytecode`, `test:desync`,
`test:inject`, `test:sched`) — which is real coverage, but it means a failure
reports as a transcript diff rather than as a named contract.

**5. A vendored 802.11 file writes the LEDs.** §7's ⚠ — small, contained, and
easy to forget until an app's LED state is stomped mid-join.

**6. A stale number in the flash budget.** `firmware/README.md` puts the two
demo assets at "4,547 B together" while naming them 2,160 B and 3,620 B, which
does not add up; `boot/README.md`'s 2,387 B for the boot chunk is what makes
4,547 correct. The 3,620 figure is the stale one. Cheap to fix, and worth fixing
because that paragraph is what a future flash-budget decision reads.

## Re-deriving this

Nothing here is hand-maintained knowledge; each claim came from a command.

```sh
# the C include graph
grep -rn '^#include' firmware/src firmware/sys/src

# which files are still twins of the mtl track
for f in $(cd firmware && ls src/hal/*.c src/usb/*.c src/net/*.c); do
  cmp -s "firmware/$f" "../mtl/firmware/$f" && echo "twin: $f"; done

# what each Lua module actually references (strip comments first)
task lua:lib:size          # per-module bytecode
task lua:firmware:build    # flash total; fails loudly on overflow
```
