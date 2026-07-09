# Provenance

Sources are **vendored** (copied, not submodules — rationale in
[NABAZTAG_SDK.md](NABAZTAG_SDK.md)). Each entry records origin repo + commit so changes
can be diffed and backported upstream. "Local changes" lists everything we altered from
the vendored copy.

## `tools/mtl_linux/`
- **Origin:** https://github.com/rngtng/mtl_linux — the MTL toolchain: **compiler AND
  simulator** (rngtng).
- **Commit:** `7606eb3ec7afa40d6af4c1d33165068e24ac07e3` (upstream HEAD). Vendored straight
  from upstream — not via nabgcc's old submodule pin. The Docker/Task/README/LICENSE and
  the GCC-14.2 + macOS + warning compile fixes all landed upstream (commits `547ce0c..7606eb3`),
  so the backport bridge is already crossed for this tree.
- **Contents:** source only — `src/ inc/ utils/ Makefile config.txt`. Build artifacts and
  the bundled `nominal.mtl` fixture are excluded; `SOURCE` is supplied at run time instead.
- **Local changes:**
  - `Taskfile.yaml` — parameterized `IMAGE`/`DIR` (so the root Taskfile injects
    `IMAGE=nabaztag-sdk`, `DIR=./tools/mtl_linux/`) and added `-w /work` to the compile run;
  - `config.txt` — dropped the hard-coded `SOURCE nominal.mtl` line (source is a run-time
    arg now), kept the `MAC` default.
- **Built by:** the tools are compiled INTO the image (`Dockerfile` runs `make comp simu`,
  copies in `config.txt`); the Taskfile exposes two verbs, surfaced at the root as
  `task mtl:compile` / `task mtl:simulate` (image `nabaztag-sdk`). Verified: compiles a
  minimal program → bytecode; simulator runs from the baked `config.txt`.
- **Role:** the **single toolchain** — compile AND simulate (§4.1, decided after
  comparing both simulators). The only gap vs piper's simu is an HTTP file server, being
  cherry-picked in (~200 LOC, piper's `http_server.c`) rather than forking a second VM.

> ⚠ **Build gotcha:** `Makefile` is matched by a *global* git ignore (`makefile`, case-
> insensitive) and so is currently **not committed** — the Dockerfile `COPY Makefile` step
> fails on a fresh clone. Fix: add `!tools/mtl_linux/Makefile` to the repo `.gitignore` and
> `git add -f tools/mtl_linux/Makefile`.

## `tools/testvm/`

- **Origin:** `nabgcc/testvm/` — native C VM smoke-runner from the
  nabgcc project (rngtng, no public commit tracked).
- **Contents:** `testvm.c`, `stubs/` (hal + usb + utils), `Makefile`, `Dockerfile`,
  `Taskfile.yaml`.
- **Local changes:**
  - `testvm.c` — fixed `extern uint8_t *dumpbc` → `extern const uint8_t dumpbc[]` and
    `loaderInit(&dumpbc)` → `loaderInit(dumpbc)` (pointer vs. array mismatch); added Unix
    DGRAM socket in `push_button_value()` (`/sock/button.sock`, same scheme as `mtl_simu`);
  - `Makefile` — rewritten to use VPATH so VM/net sources are resolved from `../src/vm/`
    and `../src/net/` (the firmware's own sources) rather than copies under `testvm/src/`;
  - `Dockerfile` + `Taskfile.yaml` — new.
- **VM sources used:** `src/firmware/src/vm/` (not nabgcc's copy — NabaztagHackKit's vm has
  three bug-fixed files: `vinterp.c`, `vloader.c`, `vmem.c`).
- **Role:** `task simulate:firmware` — compiles the **firmware's own C VM** natively (no
  ARM cross-compiler) and runs the boot bytecode through it. Catches VM bugs invisible to
  `mtl_simu` (which uses a separate VM implementation).

Local changes on top of the vendored sources:
- `testvm.c`: dropped the stray `dbg_buffer` redefinition (conflicted with the real
  `extern char dbg_buffer[DBG_BUFFER_LENGTH]` from `utils/debug.h`); replaced calls to
  `consolestr`/`EOL` (a private macro of `vm/vlog.c`, not visible outside it) with the
  underlying `putst_uart`; fixed the `dumpbc` extern to an array and the `loaderInit`
  call site to match.
- `stubs/hal/audio.c`: same `consolestr` → `putst_uart` fix.
- `Makefile`: added the firmware's own `../src/utils/debug.c` (no sim stub exists for
  it) and `-I../sys/inc` (for the SoC register headers `vinterp.c` needs); `VPATH`
  puts `stubs/utils` before `../src/utils` so `delay.c`/`mem.c`/`sys.c` still resolve
  to the sim stubs.
- `Dockerfile`: `apt-get install --no-install-recommends gcc make` silently skipped
  `libc6-dev` (only a `Recommends` of `gcc` in Debian, dropped by that flag), leaving
  no standard headers at all; added `libc6-dev` explicitly.
- `Taskfile.yaml`: the vendoring commit assumed `testvm/` lived at
  `src/firmware/testvm/`; it actually lives at `tools/testvm/` (a sibling toolchain
  layer, per this repo's "self-contained layer folders" convention). Fixed the
  `simulate`/`test`/`clean` tasks to mount `tools/testvm/` at `/work/testvm` alongside
  `src/firmware/` at `/work`, and reworked the `image` task to follow the same
  `dir: '{{.DIR}}'` / build-context-is-cwd convention as the other layers (`mtl_linux`,
  `preprocessor`) instead of a bespoke `-f` path — the original default (`DIR: "../"`)
  also silently collapsed to `"."` when included alongside sibling namespaces that use
  the `"."`-default convention, a Task templating quirk best avoided by always passing
  `DIR` explicitly from the root Taskfile (as every other included layer already does).
- Added `test:firmware` (root Taskfile) / `testvm:test` (this layer): builds `testvm`
  and runs it against real boot bytecode for a bounded window (`DURATION`, default 5s).
  Exit 0 or 124 (timed out while still running) is a pass; anything else (e.g. 139,
  segfault) fails the task — the same VM interpreter never exits on its own, so a
  bounded smoke run is the only meaningful pass/fail signal.
Per `CLAUDE.md`'s vendoring rule ("copy sources in, don't submodule — record
origin repo + commit + local changes"). This is the backport bridge: what came
from where, and what changed here so fixes can flow back upstream.

## `src/firmwareV2/lua/`

- **Origin:** https://github.com/lua/lua — PUC-Rio Lua, the reference C
  implementation. Vendored from the official release tarball
  `https://www.lua.org/ftp/lua-5.4.7.tar.gz`.
- **Version:** **Lua 5.4.7** (2024). Tarball SHA-256
  `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`.
- **Contents:** the tarball's `src/` `*.c` + `*.h` **verbatim** (the whole core +
  all stdlib source; `Makefile` and `lua.hpp` omitted). The build compiles only a
  subset (see the Makefile's `LUA_CORE`/`LUA_LIB`): core + `base`/`string`/`table`.
  The standalone `lua.c`/`luac.c` are present but unused — our own REPL is
  `src/firmwareV2/src/app/lua.c`. Keeping the full `src/` verbatim keeps the
  backport diff to exactly the two files below.
- **Local changes (2 files, all for the 124 KB flash budget / no-FPU ARM7TDMI target):**
  - `luaconf.h` — `LUA_32BITS` default flipped `0 → 1` (32-bit `int` integers +
    32-bit `float`; the chip has no FPU, so `long long`/`double` would pull in
    expensive soft-float and 64-bit helpers). One-line change, marked in-file.
    The `Local configuration` block also carries the **M7 flash-reclaim overrides**
    (#106), each marked in-file and implemented by `luai_*` helpers in
    `src/app/lua.c`: `lua_writestring`/`writeline`/`writestringerror` → semihosting
    (M7.1 #107); `lua_str2number` → a compact decimal parser instead of `strtof`
    (M7.2 #108); `luai_numpow`/`luai_nummod` → libm-free `^`/`%` (M7.3 #109).
  - `lbaselib.c` — removed the `"dofile"` and `"loadfile"` entries from
    `base_funcs[]` (their `luaL_loadfilex`→`fopen` path pulls ~8.5 KB of newlib
    stdio; this target has no filesystem). `"load"` (in-memory, used by the REPL)
    is kept. The now-unused static functions are dropped by `--gc-sections`.
- **License:** MIT (the license text is embedded in `lua/lua.h`).
- **Role:** the M4 (#92) Lua runtime. Twin to nothing in this repo — a fresh
  vendored dependency, not a copy of `src/firmware`.

**Note on the M4 host changes** (in `src/firmwareV2/`, not the vendored `lua/`):
`sys/ml67q4051.ld` gained `--gc-sections` support (`KEEP` on `.intvec`/`.startup`,
plus `__extram_start__`/`__extram_end__` for the ExtRAM `_sbrk` heap) and the
Makefile an `APP=lua` build variant. `sim/simulate.py` (#96) was fixed to load
ELF segments at their **physical (load) address** rather than the virtual address
— init.s does an LMA→VMA `.data` copy, so loading at the VMA left the flash LMA
blank and the boot copy clobbered `.data` (newlib's `_impure_ptr` → NULL, crashing
the first stdio/malloc call); it also gained `--input` to feed the REPL over
semihosting `SYS_READC`.

## Where each part came from

| Path | Origin | Notes |
|---|---|---|
| `tools/mtl_linux/` | `rngtng/mtl_linux` (dockerized in `9037084`) | MTL compiler + simulator (Linux/macOS port of Sylvain Huet's original toolchain) |
| `tools/preprocessor/` | written for this repo | pcpp-based `#include`/`#ifdef` preprocessor replacing piper's Perl one |
| `tools/mockserver/` | written for this repo | stdlib-only Python mock HTTP file server (#39) — logs requests, serves an app's `src/<app>/assets/` so the sim can fetch `init.forth`/`bc.jsp`/`*.mp3` end-to-end (`task simulate:app:*:mock`) |
| `src/firmware/` | `nabgcc` fork (`2894846`, dockerized `ed3972c`) | C bytecode VM + drivers ported to `arm-none-eabi-gcc`; WPA2 branch |
| `src/firmwareV2/lua/` | PUC-Rio **Lua 5.4.7** (`lua.org/ftp`, `lua/lua`) | vendored interpreter for the M4 (#92) Lua REPL; 2 local edits (`luaconf.h`, `lbaselib.c`) - see section above |
| `src/firmwareV2/` | original to this repo; ARM7TDMI startup/linker/register base **copied from `src/firmware/` @ `3a37cef`** | eLua-successor Lua 5.4 port to the V2 (ML67Q4051), issue #87. `sys/{ml67q4051.ld,asm/*.s,src/irq.c,inc/*.h}`, `inc/common.h`, and the HAL `src/hal/{led.c,spi.c}` + `inc/hal/{led.h,spi.h}` (LED/SPI, added at M1 #89) are verbatim copies of the vendored `nabgcc` startup/HAL, so a VM/register fix in `src/firmware` may apply here too (grep the sibling). The M8 (#116) `src/hal/audio.c` + `inc/hal/audio.h` are a **trimmed** port of `src/firmware`'s VS1003 driver (SCI read/write, volume, amplifier, built-in sine test; the SDI-stream playback and ADPCM record paths were dropped), with two fixes for a silent `nab.beep`: an RX-FIFO drain at the end of `vlsi_write_sci` and a bounded DREQ-wait in the SDI control feed. Local change: `common.h` drops the `utils/debug.h` (UART) include - the V2 board has no UART. The #123 (M8 follow-up) `vlsi_play` in `src/hal/audio.c` restores SDI-stream playback (the part M8 dropped), plus a WRAM-based endFillByte flush; `src/hal/adc.c` + `inc/hal/adc.h` are new, porting the ADC ch.2 register sequence (`ADCON0`/`ADCON1`/`ADCON2` init + the `PORTSEL2` pin mux) from `src/firmware/src/main.c` and `get_adc_value`. `src/app/gpioprobe.c` is original to this repo (issue #123) - a GPIO-scan probe in the same spirit as `audioprobe.c`/`ledmap.c`, written to find the wheel's click switch and the audio-jack signal but not yet run (no JTAG/Pi access in the session that added it). `src/app/blink.c` (M1) is original to this repo and uses a software busy-loop delay rather than the timer-IRQ `utils/delay.c` (the interrupt controller/timer are not brought up until later). `sim/simulate.py` (#96) gained an instant-SPI-completion stub at M1 (a data-register write sets the `SPIF` flag) so SPI-polling drivers can run, and at M4 (#92) an ELF-load-at-physical-address fix + `--input` REPL feeding. M4 also added `--gc-sections`/`KEEP`/ExtRAM-heap symbols to `sys/ml67q4051.ld` and an `APP=lua` build variant. The Lua 5.4 runtime is vendored under `src/firmwareV2/lua/` (see its own section above). The M9 (#117) `src/hal/i2c.c` + `inc/hal/i2c.h` are a **verbatim** port of `src/firmware`'s I2C driver (register set was already carried over in `sys/inc/ml674061.h` since M0); `src/hal/rfid.c` + `inc/hal/rfid.h` are a **trimmed** port of `src/firmware`'s CRX14 driver (init/close, the initiate/slot-marker/select anti-collision dance, frame read, UID read) - the SRIX EEPROM read/write path (`write_eeprom_rfid`/`read_eeprom_rfid` and the UID→CHIP_ID re-select helpers built on them) was dropped as out of scope for UID-only read; `rfid_read_uid()` (new, not in the original) is the single entry point the Lua binding calls. `sim/simulate.py` gained an instant-I2C-completion stub: reads of `I2CSR` are forced to always report "transfer done, no error" (`MCF` set, `RXAK`/`MBB`/`MAL` clear). This is a *read* hook, not a write-triggered one like the SPI `SPIF` stub - a write-triggered version was tried first and silently failed for `read_i2c`'s multi-byte receive loop, because that loop's own `I2CSR` clear write and the stub's fix-up write target the same register, and Unicorn's `MEM_WRITE` hook fires *before* the store commits, so the stub's write was clobbered by the original store landing right after (confirmed with a standalone Unicorn repro). `MEM_READ` hooks fire *before* the load, so forcing the value there is race-free. Without this fix multi-byte I2C reads (e.g. the CRX14's 19-byte slot-marker frame) each burned `waiti2cmcf`'s full ~1e6-iteration timeout - observed costing 5+ billion simulated instructions without completing. The stub does not model the CRX14, so simulated RFID reads return the last address byte written to `I2CDR`, not real tag data (same caveat as audio/SPI - validates code paths, not analog bus behaviour). `src/app/rfidprobe.c` (new) is a hardware bring-up probe (mirrors M8's `audioprobe.c`): confirms the CRX14 acknowledges an I2C parameter-register write/read-back round trip before the full driver is trusted, per the CLAUDE.md peripheral-exists rule - the coupler's presence is teardown-documented (`docs/hardware-dissection.md`) but not yet hardware-verified from this port (no JTAG rig access from the sandbox that wrote it; run `task repl:firmwareV2:hw APP=rfidprobe` to confirm). The M10 (#118) `src/hal/motor.c` + `inc/hal/motor.h` are a **verbatim** port of `src/firmware/src/hal/motor.c` (all three `PCB_RELEASE` branches kept, same convention as `led.c`/`spi.c`) plus one addition not in the original: `init_ears()`, replacing the motor-pin subset of `src/firmware/src/main.c`'s `init_io()` that firmwareV2 has no equivalent of. No IRQ/timer-subsystem bring-up was needed - `init_pwm()`/`run_motor()`/`stop_motor()`/`get_motor_position()` are all direct FTM register pokes/reads, no ISR. `common.h`'s `PWM_MCC1..4`/`PHASE_MCC1..2` GPIO macros (LLC2_4c and LLC2_3) were already present from the original wholesale `common.h` copy, so no header changes were needed. **Hardware-verified** via `earprobe` (both motors drove, both encoder counters advanced). One bug was found and fixed during the hardware pass: `init_ears` was missing the `PORTSEL3` pin-mux setup. `src/firmware`'s `init_io` calls `set_wbit(PORTSEL3, 0x05550000)` (with `MOTOR_SPEED_CONTROL`) to route PF0-PF5 to the FTM peripheral - PF0/PF1 for encoder-counter inputs (FTM0/FTM1) and PF2-PF5 for PWM drive outputs (FTM2-FTM5). `init_ears` only set PF0/PF1 (`0x00050000`), leaving the PWM output pins as GPIO and silencing the motors entirely. Fixed to use `0x05550000` under `MOTOR_SPEED_CONTROL`. `earprobe` also confirmed that `get_motor_position` should read `FTMnC` (the running counter) rather than `FTMnGR` (the wide-use capture register) - both tick identically in this mode, but `FTMnC` is unambiguously the counter. Motor 1 = left ear (hardware-confirmed). |
| `src/boot/` | the original Violet/IAR boot (via `firmware_nabaztag`), split into modules | recovery path — WiFi provisioning + `bc.jsp` fetch. Device build's TCP/IP/DHCP/DNS/HTTP/WiFi now compose the shared `lib/net` stack (#47, #103), driven from boot's loop via `lib/sys/task`; `ipv4.mtl`/`dns.mtl`/`http.mtl`/`wifi.mtl` are thin wrappers that alias boot's historical names, and `wifi.mtl` drives lib's button-triggered master (AP) mode + config portal via the `wifi_master_cb` seam. `config.mtl`'s flash layout is unified with `src/app-piper/utils/config.mtl` (same `conf.bin`). The `SIMU` build keeps boot's own resolver/client/WiFi machine over `tcpudp_emu.mtl` |
| `src/app-piper/` (business-logic layers: srv/, run/, chor/ protocol layers, config, app forth words) | `nabaztag-piper` (ServerlessNabaztag fork), added `3ccbf2d` | the app; most of its former bulk is now `lib/` (see below) |
| `lib/net/`, `lib/hw/`, `lib/audio/`, `lib/chor/` engine, `lib/forth/` word packs | extracted from `nabaztag-piper`'s `src/app-piper/{net,ipv4,hw,audio,chor}` | generic building blocks pulled out behind seams — see `lib/README.md` for the seam contract and CHANGELOG v0.4.0–v0.9.0 for the extraction history |
| `lib/forth/` interpreter core (interpreter, stack/arithmetic/comparison/logical/string/list/control/compile, JSON parser) | nabaztag-piper's Forth interpreter | Copyright (c) 2025 Andrea Bonomi, MIT License (see file headers) |
| `lib/forth/memory.mtl` | trimmed copy of `src/app-piper/forth/memory.mtl` | generic cell allocator + `@ ! ? +! variable`; dropped the Nabaztag/config "special address" cells (`server-url`, `leds-*`, `info-*`, …) that tie the piper version to hardware globals a generic app doesn't have, keeping only the generic `state` cell. Folded straight into `forth_core_words` (`lib/forth/dictionary.mtl`, "Memory" section) — same treatment as the JSON words — so every `lib/forth` consumer gets it for free; app-piper's own richer `src/app-piper/forth/memory.mtl` still wins at its call sites via include order. Copyright (c) 2025 Andrea Bonomi, MIT License (see file header) |
| `lib/forth/time.mtl` | the time subset of `src/app-piper/forth/misc.mtl`, extracted to a shared opt-in pack | `time&date local>string utc>string time-ms uptime ms update-time time?` — thin wrappers over `lib/sys/time.mtl`, now shared by `src/app-piper` and `src/app-template` (both include it; app-piper's `forth/misc.mtl` no longer carries its own copy). Not folded into `forth_core_words` — it reaches into the time/NTP/task subsystems, so it stays opt-in. The simulator's `ntp_get_time` stub lives app-side in `src/app-template/app.mtl`. Copyright (c) 2025 Andrea Bonomi, MIT License (see file header) |
| `lib/forth/task.mtl` | promoted from `src/app-piper/forth/task.mtl` (was byte-for-byte duplicated into `src/app-template/forth_task.mtl`) | `task-start task-stop task-suspend task-resume .tasks` — opt-in word pack over `lib/sys/task.mtl`'s scheduler, now the one true copy shared by both app-piper and app-template. Thin pack: references the scheduler symbols but does not `#include "lib/sys/task.mtl"` (that would breach the lib/forth→lib/sys boundary); the consumer pulls it in and runs `loopcb #task_scheduler`. Corrected the doc-comment word names (`TASK-*`, was `*-TASK`) and the `.tasks` description while promoting. Copyright (c) 2025 Andrea Bonomi, MIT License (see file header) |
| `lib/forth/hw.mtl` | the raw-hardware subset of `src/app-piper/forth/nabaztag.mtl`, extracted to a shared opt-in pack | `led! leds-off ears move-ear` — thin wrappers over the VM's `led`/`motorset`/`motorget` natives via `lib/hw/leds.mtl` + `lib/hw/ears.mtl`. Kept only the raw-hardware words; left piper's app-policy words behind (`sleep`/`wake-up` state machine, `volume`, `random`, the `led-*` override-cell variables, info/play). Reframed the LED words: piper exposes `led-nose`/… as `FORTH_MEMORY_LEDS_OVERRIDE_*` cells its animation loop reads — this pack instead offers a direct `led!` over `leds_set_color`, since app-template runs no such loop. `move-ear` corrected to `( ear pos dir -- )` matching `ears_go i p d` (piper's `forth_move_ear` popped into mismatched names). No `#ifdef SIMU` split: the natives are VM opcodes present in the `mtl_linux` simulator too. Copyright (c) 2025 Andrea Bonomi, MIT License (see file header) |
| `lib/forth/net.mtl` | the HTTP/WiFi subset of `src/app-piper/forth/net.mtl`, extracted to a shared opt-in pack (#57) | `http-get http-post mac ip` — thin wrappers over `lib/net`'s HTTP client (`http.mtl`) and WiFi/IP state (`wifi.mtl`, `lib/net/ipv4`). **Device-only**: `http_request`/`wifi_mac_addr` are linked only by `lib/net/net.mtl` (device build), not the simulator's `lib/net/tcp.mtl`, so consumers include the pack and its dictionary entries under `#ifndef SIMU`. Deliberately dropped piper's `tcp-listen` (callback hardwired to app-piper's `srv/telnet_server.mtl`, an app service not a lib block) and the `forth_load` init helper (piper-boot-specific), mirroring how `lib/forth/hw.mtl` leaves piper's non-raw words behind. app-piper keeps its own richer `forth/net.mtl`. Copyright (c) 2025 Andrea Bonomi, MIT License (see file header) |
| `lib/std/`, `lib/sys/` | mix of `mtl_library`'s curated stdlib and material extracted from `nabaztag-piper`'s `utils/` | — |
| Docs under `bytecode/_docs/`, MTL grammar/opcode references | `mtl_library` + the original `NabaztagHackKit` Ruby gem's `_docs/` | kept across the v1→v2 reboot |

Openocd JTAG guide/adapter configs (`8b0885c`) are original to this repo (the
host-side flashing exception noted in `CLAUDE.md`).

## Local changes vs. upstream (for backporting)

Bugs found and fixed here while building the `lib/` test suite — all present in
upstream `nabaztag-piper` at the commit vendored in `3ccbf2d`:

- **Forth `ROT`** rotated the wrong direction; **`TUCK`** behaved like `OVER`
  (`lib/forth/stack.mtl`).
- **Forth `*/` and `*/mod`** divided by the wrong stack operand
  (`lib/forth/arithmetic.mtl`).
- **JSON parser**: bare `true`/`false`/`null` inside an array or object never
  advanced the parse cursor, looping until the simulator ran out of memory
  (`lib/forth/json.mtl`).
- **Forth `write_fn` I/O path** (the socket/telnet output abstraction) had never
  actually been compiled — invalid call syntax plus a backwards `fixarg` binding
  (`lib/forth/output.mtl`).
- **`lib/sys/time.mtl` day calculation** could report the previous day (the
  `h*512/675` shortcut truncated the low word's contribution).
- **`lib/sys/time.mtl` month parsing** (`_time_parse_month`) compared a
  lower-cased input against the original-case `MONTHS` table, so HTTP `Date:`
  headers never parsed.

None of these are behavioral choices — each is pinned by a test in `test/lib/`
that documents the expected behavior; see the corresponding `CHANGELOG.md`
entries for full descriptions.

## Vendoring hygiene

- Never vendor secrets: `conf.bin`-style credential files ship only as
  sanitized `*.sample`, git- and docker-ignored for the real file.
- Build artifacts are excluded via `.gitignore`/`.dockerignore` and rebuilt in
  Docker — nothing generated is committed.
