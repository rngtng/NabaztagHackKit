# NabaztagSDK — best-of-breed synthesis across the four repos - INFO and TODO

Goal: one SDK covering **low firmware (C/ARM) → MTL development → Forth scripting**,
with a **solid, stable toolchain (build / simulate / test / flash-upload)** as the
priority. High-level apps (Home Assistant, weather, TTS) are **out of scope** —
they consume the SDK, they aren't part of it.

This doc compares `firmware_nabaztag`, `mtl_library`, `mtl_linux`, `nabgcc`, and
`nabaztag-piper`, picks the best piece for each capability, and proposes a layout.

---

## 1. The four repos at a glance

| Repo | What it really is | Era | Role in SDK |
|------|-------------------|-----|-------------|
| **firmware_nabaztag** | Original Violet/IAR firmware archive + WPA2 origin + i2c/PSK fixes + packaging tools | 2021 upload of ~2006 code | where everything came from |
| **nabgcc** | The **C bytecode VM + drivers** ported to `arm-none-eabi-gcc` | active (2026) | **Layer 0** — the firmware that gets flashed |
| **mtl_library** | The **MTL language itself**: curated stdlib + precompiler + MTL test framework + grammar/opcode docs (rngtng) | 2019 | **Layer 1+2 glue** — MTL stdlib, tests, docs |
| **nabaztag-piper** | ServerlessNabaztag fork: huge **MTL app + a Forth interpreter written in MTL** + its own MTL compiler/simulator + HTTP/telnet runtime | active (2026, this fork) | **Layer 2+3** — the app, the Forth idea, OTA/REPL |
| **NabaztagHackKit** | rngtng's Ruby gem (Gemfile/gemspec/Rakefile/lib/spec/ext) + a `bytecode/_docs/` copy of the same grammar/commands docs | 2019 | **→ the SDK shell.** Reboot as v2: keep repo+name+docs, gut the outdated Ruby/bytecode |

### The crucial relationship (these are **not** competitors)

`nabgcc` and `nabaztag-piper` solve **different layers** of the *same* stack.
The Nabaztag is a three-language cake (per `nabgcc/architecture.md`):


## 2. Capability comparison — who wins each

| Capability | firmware_nabaztag | mtl_library | nabgcc | nabaztag-piper | **Pick** |
|-----------|:---:|:---:|:---:|:---:|----------|
| C firmware / VM build (ARM) | IAR (closed) | — | **GCC + Docker + Task** ✅ | — | **nabgcc** |
| Driver/WPA2 source of truth | origin + fixes | — | **modern, audited** ✅ | — | **nabgcc** (provenance: fw_nabaztag) |
| MTL compiler (.mtl→bytecode) | — | mtl_linux (brew) | mtl_linux (submodule) | **vendored `compiler/`** | **mtl_linux** (§4.1) ✅ |
| MTL host simulator | vlispemu.exe (win) | via mtl_linux | mtl_linux `simu` | **`mtl_simu` w/ fake audio/net/http** ✅ | **mtl_linux** + ported http server (§4.1) |
| Precompiler (#include/#ifdef) | — | `preproc.pl` | — | `preproc.pl` (diverged) | **merge → one** (§4.2) |
| MTL stdlib | scattered `_sources` | **curated `lib/` + verdicts** ✅ | — | app-embedded | **mtl_library** |
| MTL test framework | — | **assertion-based, runs in simu** ✅ | — | — | **mtl_library** |
| Firmware **app** (MTL) | original boot | — | minimal boot only | **full app** ✅ | **nabaztag-piper** |
| **Forth interpreter (in MTL)** | — | — | — | **17-file impl** ✅ | **nabaztag-piper** |
| Forth runtime scripts + REPL | — | — | — | **vl/ + telnet** ✅ | **nabaztag-piper** (strip apps) |
| Flash C firmware (JTAG/recover) | J-Link pinout doc | — | **OpenOCD + GDB** ✅ | — | **nabgcc** |
| Flash C firmware (no probe) | mkfirmware.py | — | **mkfirmware.php → .sim** ✅ | — | **nabgcc** |
| Deliver MTL bytecode (OTA) | — | — | vendored in C (`bc.c`) | **HTTP `bc.jsp` pull** ✅ | **nabaztag-piper** |
| Hot-reload Forth | — | — | — | **`load-srv` / telnet** ✅ | **nabaztag-piper** |
| C-VM unit/smoke test | — | — | **`testvm/` native** ✅ | — | **nabgcc** |
| Orchestration (Docker/Task) | — | bare Makefile | **Task + 2 Docker imgs** ✅ | plain Makefile | **nabgcc** |
| Language docs (grammar/opcodes) | — | **`_docs/` grammar+commands** ✅ | architecture.md | `words.txt` (Forth) | **all 3, merge** |
| Word-reference generator | — | — | — | **`extract_words.py`** ✅ | **nabaztag-piper** |

---

## 3. The three-tier update model (the SDK's spine)

The reason to combine these repos: together they give a **graceful update gradient**
from "needs a soldering-iron JTAG probe" to "edit a text file, device reloads it."
This is exactly the "low firmware → high-level dev" spectrum requested.

| Tier | Artifact | Transport | How often it changes | Owner repo |
|------|----------|-----------|----------------------|------------|
| **T0** | C VM firmware (`Nab.bin`) | JTAG (OpenOCD+GDB) **or** config-page `.sim` | almost never (frozen VM) | nabgcc |
| **T1** | MTL app bytecode (`bc.jsp`) | HTTP pull from your web server at boot | per MTL feature | nabaztag-piper |
| **T2** | Forth scripts (`*.forth`) | HTTP `load-srv` / telnet REPL, live | per tweak, interactively | nabaztag-piper |

The "serverless" trick: flash a minimal VM/bootloader **once** (T0), then iterate the
whole app by redeploying `bc.jsp` to a local web server (T1), and hot-tweak behavior
over telnet without any recompile (T2). The SDK's job is to make all three tiers
ergonomic, simulated, and tested.

### 3.1 The missing rung: `boot.mtl` is "Layer 0.5"

Between the C VM (T0) and the remote app (T1) sits **`boot.mtl`** — MTL bytecode that
the C VM runs first. In piper this is `boot/boot.0.0.0.13.mtl` (the **original Violet
bootloader**, 75 KB). Reading it (`boot.0.0.0.13.mtl:2588`) shows exactly what it does:

```
strcatlist url::"/bc.jsp?v="::(webip FIRMWARE)::"&m="::(webmac netMac)
          ::"&l="::(webmac confGetLogin)::"&p="::(webmac confGetPwd)::"&h="::(itoa HARDWARE)
```

It brings up WiFi/DHCP/ARP, reads config from **`conf.bin`** (`confGetWifissid`,
`confGetNetgateway`, `confGetServerUrl`, `confGetLogin`, `confGetPwd`), runs an on-device
**config web server** (`startconfigserver 80` — the blue-LED setup page, with a
`getbinary` + `reboot` path that accepts a firmware `.sim` upload), then **downloads
`bc.jsp` and runs it**. So:

- **WLAN + server-url ("proxy") + login/pwd config lives in `conf.bin`** (flash), set
  via the boot's config page — *not* in source, it's runtime device state.
- **WPA2 itself is Layer 0 (C, `ieee80211.c`)**; the boot only carries the *config*
  (which SSID, which server).
- **Remote loading is NOT optional in piper** — it's the whole mechanism. Piper ships a
  full app (`firmware/`) but **does not rebuild the boot**; its Makefile compiles only
  `firmware/main.mtl → bc.jsp`. The Violet boot stays frozen in flash.

### 3.2 Two embedding strategies — the SDK should support both

| | **Remote-load** (piper) | **Monolithic** (nabgcc) |
|---|---|---|
| What's flashed | C VM + `boot.mtl` only | C VM + **app bytecode embedded** (`bc.c`) |
| App delivery | HTTP pull of `bc.jsp` at boot | baked into the firmware image |
| Iterate app | redeploy `bc.jsp`, no reflash | rebuild + reflash every time |
| Needs a server | yes (your local box) | no — runs standalone |
| Best for | day-to-day dev, the Forth/REPL loop | recovery, initial flash, server-free demo |

Same MTL source, two link targets. **Default to remote-load** (matches the dev loop you
want); keep monolithic as the recovery/initial-flash path. This is why the SDK must own
a buildable `boot.mtl`, which *neither* upstream gives you in modern form: nabgcc embeds
the whole app (no separate boot), piper uses the frozen 2006 boot. **Owning a modern,
buildable `boot.mtl` is net-new SDK work** (see §9).

---

### 4 Three test/simulate levels — keep all, they're complementary
- `nabgcc/testvm/` — compiles the **C VM** natively, smoke-runs bytecode (catches VM bugs).
- `mtl_simu` — runs an **MTL app** on host with faked HW (catches app-logic bugs).
- `mtl_library/test/` — **MTL assertion framework** run through the simulator (unit tests for stdlib).

## 7. Low-dependency stack — settle on Python + C/C++

Net: **Python is the one scripting language**; C/C++ stays only as Docker-built binaries.

**On Docker + Task — yes, this is the right call.** The two genuinely painful deps are
`arm-none-eabi-gcc` (Layer 0) and the **32-bit** MTL toolchain (`-m32`); both are
miserable to install natively on macOS/Windows and version-drift badly. Containerizing
them makes the build bit-reproducible and the host requirement just *Docker + Task* —
which is the only realistic way to support non-Linux. Task adds cross-platform task
running with the per-layer namespaces above. Two caveats, both acceptable: (1) the
32-bit MTL image is amd64-only, so it runs emulated (slow) on Apple Silicon — fine for
one-shot compiles; (2) JTAG needs real USB, so `fw:flash:jtag` runs host-side `openocd`,
outside Docker. Everything else is in-container.

---

# TODOs

The actionable roadmap now lives in **GitHub Issues** — this section keeps only
the standing context. Run `scripts/claude-setup.sh` at the start of the session,
and `task verify` before every commit — see `CLAUDE.md`.

## Where the SDK stands (07-2026)

The planned `lib/` extraction is **complete**: std/sys/net/hw/audio/chor/forth,
all with mirrored tests and seams that fail non-zero. app-piper consumes lib for
b64/url/md5/json/word/xmlparser/task/time/timezones/utils/forth-core, and
`src/app-template` + `task simulate:app` prove "new app = lib blocks + business
logic". What deliberately stayed app-side: LED animations and record-upload
flow (`lib/hw`), const_data assets + record upload (`lib/audio`), config via
`config_get_*` seams (`lib/net`), trame/streaming/interactive/info Violet-protocol
layers (`lib/chor`), and `utils/sleep.mtl` (coupled to run/chor/streaming state).

The natural next step is no longer refactoring — it's **validating on real
hardware**, then converging boot onto the proven stack. That work is tracked as
issues:

## Open roadmap → GitHub Issues

- **[#46](https://github.com/rngtng/NabaztagHackKit/issues/46)** — Validate
  `lib/net` on real hardware (flash a rabbit via JTAG). First time the
  wifi/DHCP/DNS stack runs on an actual radio; unblocks the boot work below.
- **[#47](https://github.com/rngtng/NabaztagHackKit/issues/47)** — boot/app
  convergence: port `src/boot` onto `lib/net` (deferred until #46 proves the
  stack on-device; boot is the recovery path and stays frozen until then).
- **[#48](https://github.com/rngtng/NabaztagHackKit/issues/48)** — Buildable
  `boot.mtl`: compose a modern, rebuildable boot from piper's frozen
  `boot/*.mtl`. Biggest new effort.
- **[#49](https://github.com/rngtng/NabaztagHackKit/issues/49)** — `dev:upload`
  (Python): POST a `.sim` to the rabbit's on-device config page. Small,
  high-value; makes the hardware loop faster to iterate.
- **[#39](https://github.com/rngtng/NabaztagHackKit/issues/39)** ✅ — Extend the
  simulator to serve `bc.jsp` + assets from a local Python http server.
  Done: `tools/mockserver/` (stdlib `http.server`, logs every request, serves
  files from an app's `src/<app>/assets/` folder). Run end-to-end with
  `task simulate:app:piper:mock` — the sim joins a shared docker network and is
  pointed at the mock's fixed IP (`--serverurl`), so piper fetches `init.forth`
  (and anything it references) from the mock. App-layer path wired + verified;
  serving `bc.jsp` for the boot flow is a generic follow-on (same server).
- **[#43](https://github.com/rngtng/NabaztagHackKit/issues/43)** /
  **[#42](https://github.com/rngtng/NabaztagHackKit/issues/42)** — Simulator
  Web UI (retro pixel look) + input support (button / ear / RFID).

**Tie-break** → when in doubt, prefer **nabaztag-piper**.
