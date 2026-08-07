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

Sizes below were measured, not copied: `task lua:firmware:build` for flash and
`task lua:lib:size` for bytecode, both run against this commit. Re-run them
rather than trusting this page — several of the numbers the layer READMEs
carried had drifted, and these will too.

## The stack in one picture

Five layers, read bottom-up like an address space, with one seam cutting across.
Every size is measured (`task lua:firmware:build`, `task lua:lib:size`).

```
 L4  LUA USERLAND ─────────────────────────────────── RAM · 50,973 B · 19 modules
     lib/net  12 mod · 35,585 B     lib/audio  4 · 7,011 B     apps/  10 demos
     lib/sys   2 mod ·  4,702 B     lib/hw     1 · 3,675 B
     no require · no manifest · no bundler — chunks extending a global table
 ────────────────────────────────────────────────────────────────────────────────
 L3  LUA RUNTIME SURFACE ────────────────────────────────── flash · 3,674 B chunk
     boot.lua → sched          pump · unpump · spawn · sleep   (the reactor)
     stdlib                    base (−dofile/loadfile) · string · table
                               coroutine (2,300 B — what the reactor is built on)
     absent                    math · io · os · package · debug · utf8
                               load() takes bytecode only → lundump is the whole
                               input surface
 ╔══ SEAM ═══ nab.* ═══ 38 names → 37 C functions ══════════════════════════════╗
 ║  LEDs 3 · audio out 8 · audio in 5 · wifi 8 · rfid/button/wheel/ears 7       ║
 ║  time+events 4 · persistence 2 · codec diagnostics 2                         ║
 ║  advance the reactor while blocking:  nab.wait · nab.play · nab.wifi_recv    ║
 ║  freeze it:  nab.wifi 30 s · nab.record 30 s · wifi_up 10 s · wifi_scan 5 s  ║
 ╚══════════════════════════════════════════════════════════════════════════════╝
 L2  THE LUA HOST ──────────────────────────── src/main.c · 1,444 ln · fan-out 17
     38 bindings · #LC frame REPL · dispatch_events (the pump)
     _sbrk → ExtRAM · _read/_write → UART0 · open_trimmed_libs
     the ONLY TU where lua_State exists — and 31 cohesion clusters wide
 ────────────────────────────────────────────────────────────────────────────────
 L1  DRIVERS + SERVICES ────────────────────────────────────────────────────────
     src/hal/    12 drivers · 3,119 ln   spi led button audio adc i2c rfid
                                         motor uart wifi config ota
     src/utils/  event · fmt · lcframe · libc_shim
     src/usb/    OHCI + RT2501 · 5,522 ln                          (vendored, -Os)
     src/net/    802.11 + WPA2-CCMP · 4,203 ln                     (vendored, -Os)
     lua/        PUC-Rio 5.4.7, parser removed · 30,280 ln    (vendored, 4 edits)
 ────────────────────────────────────────────────────────────────────────────────
 L0  STARTUP + SILICON ─────────────────────────────────────────────────────────
     sys/        init.s (PLL · EMC · stacks) · tick.c (1 ms) · irq.c
                 ml67q4051.ld · OKI register headers
     board       ML67Q4051 ARM7TDMI @32 MHz · flash 124 KB + 4 KB config sector
                 ExtRAM 1 MB @0xD0000000 · TLC594x LEDs (SPI1) · VS1003B (SPI0)
                 CRX14 RFID (I2C) · ear PWM (FTM) · wheel (ADC ch.2) · UART0
                 RT2501 USB WiFi (ML60842 OHCI)
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

`bin/firmware.elf` = **119,332 B of 126,976 B**, **7,644 B free** (measured, not
quoted). Roughly: ~23 KB USB + 802.11/WPA2, ~3.2 KB the reactor (`coroutine`
2,300 B measured), ~2.1 KB provisioning plumbing, ~1.5 KB the event core,
3,674 B the resident boot chunk, 2,160 B the `nab.tone()` MP3, 836 B
`nab.config`, ~0.8 KB the raw-frame/AP bindings, ~0.65 KB the OTA writer, 560 B
the scan bindings, 552 B the stream HAL. Full breakdown and the two levers that
keep it from being worse: [`firmware/README.md`](firmware/README.md).

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
| `boot/boot.lua` | `sched` — the cooperative reactor — + demo helpers | **flash** | 3,674 B |
| `lib/net/` | link, arp, ipv4, udp, dns, dhcp, tcp, http, iface, setup, provision, ota | RAM | 35,585 B |
| `lib/audio/` | player, stream, midi, volume | RAM | 7,011 B |
| `lib/sys/` | ntp, time | RAM | 4,702 B |
| `lib/hw/` | ears | RAM | 3,675 B |
| `apps/` | 10 demo apps | RAM | — |

**50,973 B of bytecode across 19 modules**, against 7,644 B of free flash
(`119,332` of `126,976` used).

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

Counted as distinct symbols crossing a directory boundary — the intended
downward flow first, then the four edges that run against it:

```
              ┌───────────────┐
              │  src/main.c   │
              └─┬───────────┬─┘
      44 syms ↓             ↓ 6 syms                       ② _write ⚠
    ┌─────────────┐     ┌──────────────┐  ◄╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┐
    │  src/hal/   │     │  src/utils/  │                          ╎
    └─┬────┬────┬─┘     └──────────────┘                          ╎
10 ↓    2 ↓    ↓ 8              ▲ ④ rand() ⚠ (2, benign) ╌╌╌╌╌╌┐  ╎
 ┌──────────┐  │  ┌──────────┐                                ╎  ╎
 │ src/usb/ │◄─┼──┤ src/net/ │╌╌╌╌╌► ③ set_led ⚠ ──────────────┴──┘
 └────┬─────┘16│4 └────┬─────┘        (into hal/, lateral)
    3 ↓  └─────┴────►  ↓ 1
 ┌──────────────────────────────────────────────────────────────────┐
 │  sys/     ╌╌╌╌► ① led_fade_tick ⚠ ──► hal/led.c                  │
 └──────────────────────────────────────────────────────────────────┘
```

`main.c → sys/` (5 symbols) is omitted above for legibility. `usb ↔ net` is a
genuine cycle — 16 symbols out, 4 back — so those two vendored directories are
effectively one 9,725-line module.

The four ⚠ edges, all measured, none accidental:

1. **`sys/src/tick.c` → `hal/led.c`** (`led_fade_tick`). The 1 ms tick ISR steps
   the fade engine, so the bottom layer depends on a driver — and `led.c` masks
   that same IRQ around its SPI flush in return. Documented at both ends, and
   still a loop.
2. **`utils/fmt.c` → `main.c`** (`_write`). A true two-way cycle: `main.c` needs
   `fmt.c`'s `snprintf` and Lua number hooks, `fmt.c` needs `main.c`'s UART
   syscall.
3. **`net/eapol.c` → `hal/led.c`** (`set_led`). The PBKDF2 loop (`F()`) drives
   LEDs 1-3 as a progress indicator and pumps `usbhost_events()` + drains RX
   from inside the key derivation. A WPA2 join therefore **writes the LEDs
   behind the app's back**, bypassing the seam entirely. Vendored, predating the
   seam, and reachable from `nab.wifi(ssid, psk)` on every join.
4. **`net/{eapol,ieee80211}.c` → `utils/libc_shim.c`** (`rand`, 2 symbols).
   Benign — the shim is a libc replacement, not a layer. It exists because
   newlib's `rand` drags ~9 KB of `vfprintf`/FILE machinery into a 124 KB budget.

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

`lib/net/link.lua` and `lib/sys/ntp.lua` are the only leaves. `iface.lua` (401
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

**2. The Lua userland has no delivery mechanism.** 50,973 B of bytecode in 19
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

**6. Size figures drift, and nothing catches it.** Three documented numbers for
the two demo assets disagreed — `firmware/README.md`'s "4,547 B together" with
the boot chunk at 3,620 B, against `boot/README.md`'s 2,387 B for that same
chunk. Measured off a real build, the chunk is **3,674 B** and the pair is
**5,834 B**; both READMEs are corrected. The `lib/` figures had drifted the same
way (`audio/player` 2,649 → 2,897, `hw/ears` 3,424 → 3,675, while `net` and
`sys` were still exact). `task lua:firmware:build` and `task lua:lib:size` print
all of it in seconds, so the gap is not measurement cost — it is that nothing
compares the printed number against the written one. This is the cheapest
unclaimed gate in the track.

## 9. Is it well structured? — coupling and cohesion, measured

Short answer: **yes on coupling, yes on cohesion everywhere except two places** —
and both exceptions are structural consequences of one decision, not sloppiness.

### Coupling: low, and pointing the right way

Dependencies should flow from things that change often toward things that don't.
Measured as instability (I = fan-out ÷ (fan-in + fan-out), 0 = depended-upon
bedrock, 1 = pure consumer), both halves of the track get this right without any
document having told them to:

| | most stable (I≈0) | most unstable (I≈1) |
|---|---|---|
| **C** | `hcdmem.c`, `irq.c`, `button.c`, `spi.c`, `uart.c`, `motor.c`, `adc.c`, `ota.c`, `lcframe.c` — fan-out **0** | `main.c` (I=0.94), `hal/wifi.c` (0.88), `hcd.c` (0.80) |
| **Lua** | `net.link` (fan-in **7**, fan-out **0**), `net.http` (3/0), `net.dns`, `sys.ntp` — 9 of 20 modules are pure leaves | `net.iface` (fan-out 10, I=0.91), `net.setup` (5), `net.provision` |

Nothing depended on by many things depends on much itself. The hubs are
composition roots — `iface.lua` exists precisely to know about ten modules —
which is where high fan-out belongs.

**Layer violations are rare and all four are known.** Across every non-vendored
TU there are exactly four upward call edges: `sys/src/tick.c` → `led_fade_tick`,
`src/utils/fmt.c` → `_write` (a genuine `main.c` ↔ `fmt.c` cycle), and
`net/{eapol,ieee80211}.c` → `rand()` (benign — `libc_shim.c` is a libc
replacement, not a layer). Plus the lateral `net/eapol.c` → `set_led` from §7.
For bare-metal C with two vendored subsystems, four is a low number.

The one real cycle is **`usb` ↔ `net`**: 12 symbols out, 4 back. Vendored, so
not this track's doing, but it means those two directories are effectively one
9,725-line module that cannot be reasoned about separately.

### Cohesion: high in the HAL, high in `lib/`, low in exactly one file

Clustering each C file's functions by shared file-scope state and internal
calls (an LCOM-style count — 1 cluster = every function belongs together):

| Single-cluster (cohesive) | Fragmented |
|---|---|
| `audio.c` 19 fns → **1**, `rfid.c` 13 → **1**, `aes128.c` 15 → 1, `eapol.c` 14 → 1, `led.c` 8 → 1, `event.c` 6 → 1, `config.c` 4 → 1 | **`main.c` 65 fns → 31 clusters** |

The other multi-cluster files are false positives: `spi.c` (5 → 5), `adc.c`,
`button.c` are stateless register pokes, so there is no shared state to cluster
on. `main.c` is not a false positive.

### The one structural defect: `main.c`

Every metric puts it alone at the end of the distribution — 1,444 lines, 65
functions, **fan-out 17** where the next highest is 7, I=0.94, 31 cohesion
clusters. It is seven modules sharing a file:

```
[20] the REPL: console, #LC frame reader, event dispatch, boot, main
[ 7] record + WAV/RIFF assembly          [ 5] wifi bindings + arg checking
[ 4] LED bindings + colour checking      [ 2] beep   [ 2] config
[25] singleton bindings (each a thin arg-check over one HAL call)
```

The 25 singletons are fine — a binding table *should* be a list of thin
wrappers. The problem is the six islands of real logic that came along for the
ride, and the cause is structural rather than careless: `main.c` is the only
place `lua_State` exists, so anything needing one lands there, and once there it
can never be linked by a test. `utils/lcframe.c` is the proof that extraction
works — it was pulled out, and it gained a unit test the moment it left.

**Two things are on the wrong side of the seam entirely.** `nab.rec_wav` is
`string → string`: it touches no hardware, yet ~44 lines of RIFF assembly sit in
flash — the scarcest resource here, ~7.5 KB free — and cannot be unit-tested,
while `lib/audio/` exists, costs no flash and already has a test harness. The
2,160 B `nab.tone()` MP3 blob is the same category. Principle 1 says behaviour
belongs in Lua; these are behaviour.

### The second-order problem: the reactor-attachment protocol has no owner

Six objects in `lib/` are pull-style state machines, and they agree on nothing:

| Object | Step verb | Self-registers with `sched`? |
|---|---|---|
| `hw.ears`, `audio.player` | `:step()` | yes — via `:attach()`/`:detach()` |
| `audio.volume` | `:step()` | no |
| `net.iface`, `net.tcp`, `audio.stream` | `:poll()` | no |

`:attach()`/`:detach()` are duplicated **byte-for-byte** between `hw/ears.lua`
and `audio/player.lua` (only the comments differ). `sched` is resident in flash
and is the obvious owner of a protocol every reactor participant needs.
Relatedly, [`boot/README.md`](boot/README.md) states that `net.iface:poll()` is
"reachable via `:attach()`" — `iface.lua` contains zero occurrences of the word.

**The seam's semantics are also patched from above.** `nab.on` holds one
callback per name and replaces silently, so `boot.lua` monkey-patches `nab.on`
to stop an app displacing `sched.tick`. The contract is now defined in two
languages and two artifacts and depends on load order — code that captured
`nab.on` before the boot chunk ran gets the C behaviour — and the
single-subscriber limit still stands for `"button"` and `"rfid"`.

### Smaller structural notes

- **`common.h` is a god header**: included by 41 files, and it mixes register
  access macros with domain constants from two different peripherals
  (`FORWARD`/`REVERSE` from the motor, `TURN_ON_AUDIO_AMPLIFIER` from the
  codec). Vendored, which explains it — but it means the one-file-per-peripheral
  cohesion is undone at the header level.
- **Host-test coverage tracks the wrong axis.** Six of our 20 TUs are
  host-tested (plus 2 vendored). The stub technique that makes register-only
  drivers testable — `test/host/stubs/ml674061.h` shadowing the absolute
  addresses — already works and is used for `i2c.c` and `adc.c`. `button.c`,
  `motor.c`, `spi.c`, `uart.c`, `ota.c` and `led.c` are equally register-only
  and equally testable. The Makefile's comment that "anything touching
  `ml674061.h` cannot be compiled for the host" is disproven by its own
  `i2c_test`/`adc_test` rules.
- **Duplication that is *not* a smell**: `arp.lua` and `dns.lua` each carry a
  two-line bounded-cache cap-and-clear, and `dns.lua` says out loud that it is
  copying `arp.learn`. Extracting a two-line policy would cost more bytecode
  than it saves on a device where every module ships over a 333 B/s console.

## Re-deriving this

Nothing here is hand-maintained knowledge; each claim came from a command.

```sh
# the C include graph
grep -rn '^#include' firmware/src firmware/sys/src

# §9's coupling/cohesion numbers: symbol-level call graph -> fan-in/fan-out,
# and functions clustered by shared file-scope state (see the commit that
# added this section for the scripts)

# which files are still twins of the mtl track
for f in $(cd firmware && ls src/hal/*.c src/usb/*.c src/net/*.c); do
  cmp -s "firmware/$f" "../mtl/firmware/$f" && echo "twin: $f"; done

# what each Lua module actually references (strip comments first)
task lua:lib:size          # per-module bytecode
task lua:firmware:build    # flash total; fails loudly on overflow
```
