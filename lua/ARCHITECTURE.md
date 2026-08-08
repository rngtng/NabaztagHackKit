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
`task lua:lib:size` for bytecode, both re-run after #332 landed. Re-run them
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
 L2  THE LUA HOST ──────────────────────────── src/main.c · 1,382 ln · fan-out 17
     38 bindings · #LC frame REPL · dispatch_events (the pump)
     open_trimmed_libs · init_hw
     the ONLY TU where lua_State exists — and ~28 cohesion clusters wide
 ────────────────────────────────────────────────────────────────────────────────
 L1  DRIVERS + SERVICES ────────────────────────────────────────────────────────
     src/hal/    12 drivers · 3,119 ln   spi led button audio adc i2c rfid
                                         motor uart wifi config ota
     src/libc/   keep-newlib-out-of-flash, and nothing else (#324):
                 libc_shim (rand/srand/__assert_func) · syscalls
                 (_read/_write → UART0, _sbrk → ExtRAM, halting abort)
     src/utils/  event · fmt · lcframe
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

**Two edges here are worth knowing about:**

- `sys/src/tick.c` → `hal/led.c` used to be a call by name, so the bottom layer
  depended on a driver while `led.c` masked that same IRQ in return — a loop.
  **#325 inverted it**: `tick_set_hook()` takes one callback, and `led_fade()`
  installs `led_fade_tick` when a fade is actually armed. `sys/` now includes no
  `hal/` header. Registering from `led_fade()` rather than from
  `init_led_rgb_driver()` is what makes it pay: an image that only ever calls
  `set_led()` never takes the engine's address, so `--gc-sections` drops it
  (`usbprobe` 9,956 → 8,908 B, `wifiprobe` 29,406 → 28,806 B; the product pays
  40 B for the layering).
- `src/utils/event.c` → `hal/button.h`, `hal/rfid.h`: the event core sits
  *above* the HAL, not beside it. That is why it host-tests cleanly (its
  dependencies are two functions) and why `button.c` deliberately holds no
  debouncing.

`src/libc/` (#324) sits **above** `hal/`, not below it: `syscalls.c` reads and
writes the console through `hal/uart`. It is a substitution layer, not a leaf —
`libc_shim.c` alone depends on nothing, but the folder as a whole does not.

## 3. What else the C firmware holds

| Area | Files | Lines | Origin |
|---|---:|---:|---|
| `lua/` — PUC-Rio Lua 5.4.7 | 61 | 30,280 | vendored, 4 local edits |
| `src/usb/` — OHCI host + RT2501 | 19 | 5,522 | vendored from mtl/V1 |
| `src/net/` — 802.11, EAPOL, AES-128, hashes | 8 | 4,203 | vendored from mtl/V1 |
| `src/hal/` | 24 | 3,119 | ported from `mtl/firmware`, then diverged |
| `examples/` — one-peripheral bring-up progs | 19 | 3,222 | original |
| `sys/` — startup, tick, irq, linker, regs | 11 | 2,457 | copied from `mtl/firmware` |
| `src/utils/` — event, fmt, lcframe | 10 | 1,516 | original |
| `src/main.c` — the Lua host | 1 | 1,382 | original |
| `src/libc/` — the newlib substitutions | 3 | 206 | original (#324) |

Of the vendored Lua tree the build compiles a **subset**: 16 core files (of 19
— `lcode`/`llex`/`lparser` are dropped, ~18.9 KB) plus `lauxlib`, `lbaselib`,
`lstrlib`, `ltablib`, `lcorolib`. `ldump` goes too — the device loads bytecode
and never writes it. The two remaining references into the dropped parser
(`f_parser` in `ldo.c`, `luaX_init` in `lstate.c`) are guarded by
`-DLUA_NOPARSER`, which is one of the **four local edits** that a Lua upgrade
must re-apply (the others: `luaconf.h`'s `LUA_32BITS` + off-newlib number and
console hooks, and `lbaselib.c` dropping `dofile`/`loadfile`).

`src/utils/` and `src/libc/` are small but load-bearing, and two of their files
are global in effect:

- **`fmt.c`** overrides `snprintf`/`vsnprintf` for *every* target and supplies
  Lua's `luai_*` number hooks — which is why `-Ilua` is on the include path even
  for examples that never link Lua.
- **`libc/libc_shim.c`** supplies local `rand`/`srand`/`__assert_func`. Without it
  the vendored net stack's `rand()` re-links ~9 KB of newlib `vfprintf`/FILE
  machinery. Any new libc call needs the same audit — via the map's *Archive
  member* section, not `--gc-sections`.

### Flash budget

`bin/firmware.elf` = **119,316 B of 126,976 B**, **7,660 B free** (measured, not
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

**50,973 B of bytecode across 19 modules**, against 7,660 B of free flash
(`119,316` of `126,976` used, post-#332).

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

Counted as distinct symbols crossing a directory boundary. **#332 cleared three
of the four edges that used to run against the layering**; what is left is the
one that was never a violation:

```
                 ┌───────────────┐
                 │  src/main.c   │
                 └─┬───────┬───┬─┘
         43 syms ↓        6 ↓   ↓ 2
       ┌─────────────┐  ┌──────────────┐  ┌─────────────┐
       │  src/hal/   │  │  src/utils/  │─►│  src/libc/  │  ② now an ordinary
       └─┬────┬────┬─┘  └──────┬───────┘1 └──────┬──────┘     downward edge:
   10 ↓    2 ↓    ↓ 9          │ 2               │ 2           fmt.c → a header,
 ┌──────────┐  ┌──────────┐    ▼                 ▼             not into main.c
 │ src/usb/ │◄─┤ src/net/ │──► hal/ (event core) hal/uart
 └────┬─────┘17│4 └──┬──┬─┘
    3 ↓  └─────┴───► │  └╌╌╌╌► ④ rand() ╌╌► libc/   (still "upward" — and still
      │ 1            │                             not a violation: see below)
 ┌────┴──────────────┴──────────────────────────────────────────────────┐
 │  sys/    ① inverted: tick_set_hook(), no hal/ include at all         │
 └──────────────────────────────────────────────────────────────────────┘

   ① ③ cleared by #332   ② resolved by naming the seam   ④ unchanged
```

`main.c → sys/` (5 symbols) is omitted for legibility. `usb ↔ net` is a genuine
cycle — 17 symbols out, 4 back — so those two vendored directories are
effectively one 9,725-line module.

`src/libc/` sits **above** `hal/` (its `syscalls.c` drives `hal/uart`), so ④ is
still upward when measured by folder rank. That is the same artifact as before,
relocated — and no further move would help, because ④ is not a dependency
between layers of this firmware at all (see below). Note also that
`firmware/README.md` calls `src/libc/` "a leaf": it is a substitution *layer*,
and only `libc_shim.c` inside it depends on nothing.

Four edges used to run against the layering, and **they were four different
things** — which is exactly why three could be closed and one could not:

1. ✅ **`sys/src/tick.c` → `hal/led.c`** (`led_fade_tick`) — *was real coupling,
   benign.* **Inverted by #325.** `tick_set_hook()` holds one callback;
   `led_fade()` installs `led_fade_tick` inside the critical section it already
   takes, so the hook and the armed fade become visible to the ISR together.
   Registering there rather than at `init_led_rgb_driver()` is what makes it
   pay — an image that only calls `set_led()` never takes the engine's address.
   Verified: `sys/` includes no `hal/` header, and `led_fade_len[]` is armed in
   exactly one place, after the install.
2. ✅ **`utils/fmt.c` → `_write`** — *was never a cycle.* `_write` is a
   **link-time seam**: the device links the UART implementation,
   `test/host/fmt_test.c` links its own buffer-capture one to assert
   `luai_writestring` byte for byte. The static analysis attributed the symbol
   to `main.c` because that is where the device copy lived. The real (smaller)
   defect was that the seam was *unnamed*. **#324 named it**: `inc/libc/syscalls.h`,
   which `fmt.c` now includes instead of forward-declaring inline.
3. ✅ **`net/eapol.c` → `hal/led.c`** (`set_led`) — *the one real defect.*
   **Removed by #323.** The PBKDF2 loop (`F()`) drove logical LEDs 1–3 — left,
   belly, right via `led_logical[]` — from inside key derivation, so a WPA2 join
   blinked three of the five for the whole 4096-iteration derivation, bypassing
   the seam. The `usbhost_events()` + `CLR_WDT` + RX drain in that loop stayed:
   without them the USB stack dies and the watchdog fires mid-derivation.
   `test/host/eapol_test.c`'s `pmk-leds` scenario now pins it, with two
   non-vacuous guards (both PBKDF2 halves run their full 4096 rounds; the pump
   still fires 63 times per half) so it cannot pass having derived nothing —
   independently confirmed to fail against a re-added `set_led`.
4. ⬜ **`net/{eapol,ieee80211}.c` → `libc/libc_shim.c`** (`rand`, 2 symbols) —
   *not a layering problem at all, and no file move can fix it.* `rand()` is a
   C standard library function; any module may call it at any layer. The edge
   exists in this graph only because the firmware **implements libc itself** and
   the analysis ranks by folder — had `rand` come from newlib's archive, no
   directory-based measurement would show an edge. #324 relocating the shim
   changed nothing, and relocating it again would not either. The correct fix is
   to the *analysis*: treat the libc substitutions as external, the way
   `<string.h>` is treated. Listed here so nobody re-discovers it as a bug.

   **Following it did turn up something real, though — just not about layers.**
   The two call sites are `eapol.c:370` (`randbuffer(snonce, …)`, the WPA2
   4-way-handshake SNonce) and `ieee80211.c:863` (an auth-frame IV), and the
   generator behind them is seeded with a compile-time constant
   (`rand_state = 0x2545f491`) that **`srand()` never overwrites — there is no
   call to it anywhere in the tree**. So every boot produces a byte-identical
   SNonce. `libc_shim.c`'s own header says as much and correctly notes it is not
   a regression: newlib's `rand()` was equally unseeded, so `mtl/firmware` has
   the same property. It bounds well short of a break — the PMK comes from the
   passphrase, so the PTK is not derivable without it — but it removes one of
   the two independent contributions to per-session PTK freshness, leaving the
   AP's randomness carrying it alone. Tracked separately; it is a crypto-hygiene
   finding that the "benign layering artifact" label was hiding.

**Cost of clearing them: −16 B net** (−64 eapol, +8 the `console_last_rx_ms`
accessor, +40 the tick hook), and every bring-up example got smaller or stayed
level.

### Where the libc substitutions belong — and what the move actually bought

Edges ② and ④ looked like the same finding wearing two hats: this firmware
carries a real, named concern — **everything that exists to keep newlib out of
the flash budget** — and it had no folder, so its pieces sat across three files
and got mis-ranked by any structural analysis, including this page's.

#324 gave it one. `src/libc/` now holds `libc_shim.c` (`rand`, `srand`,
`__assert_func`, moved verbatim) and `syscalls.c` (`_write`, `_read`, `_sbrk`,
`abort`, lifted out of `main.c`), with `inc/libc/syscalls.h` naming the
interface. `fmt.c`'s `snprintf` half stayed in `utils/`: it shares static
helpers (`pf_emit`, `pf_pad`, `pf_utoa`, `dec_shifted`) with its `luai_*` half
and is host-tested as one unit, so splitting it would duplicate or export those.

**One prediction on this page was wrong.** It said `libc/` would "then sit below
everything as a true leaf", making both ② and ④ ordinary downward edges. Only ②
came true. `syscalls.c` drives the console through `hal/uart`, so the folder
sits *above* `hal/`, and `net/` calling `rand()` is still upward by folder rank —
the artifact moved with the file rather than being dissolved by it. Only
`libc_shim.c` in isolation is a leaf; `firmware/README.md` calls the folder one,
which is worth correcting to "a substitution layer".

What the move did buy is real, and it is ② plus testability: `fmt.c` depends on
a declared header instead of forward-declaring into `main.c`, the `_write`
substitution point is finally *named* where `test/host/fmt_test.c` already
exercises it, and the console syscalls left the one TU nothing can link — which
is what unblocks #328. Measured at **+8 B**, all of it the
`console_last_rx_ms()` accessor that keeps the RX timestamp encapsulated; a
control build publishing the variable as `extern` came in at exactly 0 B, so the
mechanical move really was free.

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

**1b. And the one primitive that does pump stops pumping when it is nested.**
`dispatch_events` carries a `static uint8_t busy` re-entrancy guard: on a nested
call it runs `event_pump()` (hardware polling only) and returns before any Lua
runs. The guard is *necessary* — without it `nab.wait` inside a callback would
recurse into dispatch until the stack gave out. But it means `nab.wait(500)`
called from inside a `sched` pump, a spawned task, or a `nab.on` callback is 500
ms in which **no other pump runs, no task resumes and no queued event is
delivered** — precisely the hole #283 exists to close, reopened one level down.

Three things make this a trap rather than a documented trade-off. The `nab`
reference describes `nab.wait(ms)` as "sleep ~ms while running the event pump,
so `nab.on` callbacks fire", which is true at top level and false everywhere
inside the reactor; the consequence is written down only in a C comment in
`main.c`. `boot/test/run.lua`'s `nab_pump()` is a plain function call that does
not model the guard at all, so the suite that exists to protect the reactor
cannot catch this class of regression — the one place that harness models the
seam conveniently rather than faithfully. And the correct alternative depends on
context: `sched.sleep(ms)` inside a task, a `:step()` that returns inside a pump,
never `nab.wait`.

`lib/` gets this right today — `player:wait()` and `ears:wait()` call the
injected `sleep(0)`/`sleep(1)` as a pump-once, and long waits go through
`sched.sleep`. Nothing enforces it for app code.

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

**4. `main.c` is 1,382 lines that nothing can link.** It carries `main()`, so no
host test can reach it. The repo's own rule sends new non-wiring logic to its
own TU, and `utils/lcframe.c` shows the pattern working — as does #324, which
took the four newlib syscalls out to `src/libc/` and shed ~60 lines. Still
inside `main.c` with a rule to them and no unit test: the WAV/RIFF header
assembly (a byte-identical-to-mtl contract), the hex-frame reader and
`drop_lc_payload` resync path, and `dispatch_events`' ordering guarantees. They
are covered end-to-end by the sim goldens (`test:bytecode`, `test:desync`,
`test:inject`, `test:sched`) — real coverage, but a failure reports as a
transcript diff rather than as a named contract. Tracked as #326, with #327-#329
the extractions; #324 landing also unblocks #328's test, since the console is
now injectable behind a header.

**5. ~~A vendored 802.11 file writes the LEDs.~~** Fixed by #323 — see §7.

**5b. The WPA2 SNonce is the same on every boot.** Not a layering finding — it
surfaced from following edge ④ to what it feeds. `rand()` is seeded with a
compile-time constant and `srand()` is never called anywhere in the tree, so
`eapol.c:370`'s `randbuffer(snonce, …)` produces a byte-identical nonce every
boot, on every unit. `libc_shim.c` documents this in its own header and is right
that it is not a regression (newlib's `rand()` was equally unseeded, so
`mtl/firmware` shares it). It is bounded — the PMK comes from the passphrase, so
the PTK is not derivable without it — but it removes one of the two independent
contributions to per-session PTK freshness. There is no hardware entropy source
on this part, so the realistic fix is "seed from something that varies (tick at
first radio use, mixed with scan RSSI) and state the limits", not "make it
cryptographic".

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

**Layer violations: one remains, and it is not a defect.** There were four
upward or lateral call edges across the non-vendored tree; §7 separates them and
**#332 closed three** — the `tick.c` → `led_fade_tick` coupling (inverted to a
hook), the unnamed `fmt.c` → `_write` seam (given a header), and the one actual
defect, `net/eapol.c` → `set_led`. What is left is `net` → `rand()`, which is an
artifact of ranking layers by folder rather than a dependency anyone should
remove. For bare-metal C with two vendored subsystems, that is a clean graph.

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

Every metric puts it alone at the end of the distribution — 1,382 lines, 61
functions, **fan-out 17** where the next highest is 7, I=0.94, 28 cohesion
clusters. It is six modules sharing a file:

```
[19] the REPL: console, #LC frame reader, event dispatch, boot, main
[ 7] record + WAV/RIFF assembly          [ 5] wifi bindings + arg checking
[ 4] LED bindings + colour checking      [ 2] beep   [ 2] config
[22] singleton bindings (each a thin arg-check over one HAL call)
```

The singletons are fine — a binding table *should* be a list of thin wrappers.
The problem is the five islands of real logic that came along for the ride, and
the cause is structural rather than careless: `main.c` is the only place
`lua_State` exists, so anything needing one lands there, and once there it can
never be linked by a test.

**Extraction demonstrably works, twice now.** `utils/lcframe.c` was pulled out
and gained a unit test the moment it left; #324 took the four newlib syscalls to
`src/libc/`, which cost 0 B for the mechanical move (measured against a control
build) and turned the `_write` seam from an inline forward-declaration into a
named header. Three clusters remain worth extracting — #327, #328, #329.

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
