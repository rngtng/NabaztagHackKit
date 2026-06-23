# NabaztagSDK — best-of-breed synthesis across the four repos

Goal: one SDK covering **low firmware (C/ARM) → MTL development → Forth scripting**,
with a **solid, stable toolchain (build / simulate / test / flash-upload)** as the
priority. High-level apps (Home Assistant, weather, TTS) are **out of scope** —
they consume the SDK, they aren't part of it.

This doc compares `firmware_nabaztag`, `mtl_library`, `nabgcc`, and
`nabaztag-piper`, picks the best piece for each capability, and proposes a layout.

---

## 1. The four repos at a glance

| Repo | What it really is | Era | Role in SDK |
|------|-------------------|-----|-------------|
| **firmware_nabaztag** | Original Violet/IAR firmware archive + WPA2 origin + i2c/PSK fixes + packaging tools | 2021 upload of ~2006 code | **Provenance / reference** — where everything came from |
| **nabgcc** | The **C bytecode VM + drivers** ported to `arm-none-eabi-gcc`, Dockerized with Task, security-audited | active (2026) | **Layer 0** — the firmware that gets flashed |
| **mtl_library** | The **MTL language itself**: curated stdlib + precompiler + MTL test framework + grammar/opcode docs (rngtng) | 2019 | **Layer 1+2 glue** — MTL stdlib, tests, docs |
| **nabaztag-piper** | ServerlessNabaztag fork: huge **MTL app + a Forth interpreter written in MTL** + its own MTL compiler/simulator + HTTP/telnet runtime | active (2026, this fork) | **Layer 2+3** — the app, the Forth idea, OTA/REPL |
| **NabaztagHackKit** | rngtng's Ruby gem (Gemfile/gemspec/Rakefile/lib/spec/ext) + a `bytecode/_docs/` copy of the same grammar/commands docs | 2019 | **→ the SDK shell.** Reboot as v2: keep repo+name+docs, gut the outdated Ruby/bytecode |

> **Family note:** `mtl_library`, `mtl_linux`, and `NabaztagHackKit` are all **rngtng** repos you control — free to restructure across them.

### The crucial relationship (these are **not** competitors)

`nabgcc` and `nabaztag-piper` solve **different layers** of the *same* stack.
The Nabaztag is a three-language cake (per `nabgcc/architecture.md`):

```
 ┌─ Layer 3  Forth scripts (vl/*.forth)        edit-at-runtime, REPL      ← nabaztag-piper/vl
 │              ▲ interpreted by
 ├─ Layer 2  MTL app  (firmware/*.mtl)         incl. a Forth interpreter  ← nabaztag-piper/firmware
 │           + MTL stdlib (lib/*.mtl)          written in MTL             ← mtl_library/lib
 │              ▲ compiled to bytecode by
 │           MTL toolchain (mtl_comp/simu)     host-side                  ← nabgcc(mtl_linux) ⚔ nabaztag-piper/compiler
 │              ▲ runs on
 └─ Layer 0  C bytecode VM + drivers + WPA2    bare-metal ARM7TDMI        ← nabgcc  (origin: firmware_nabaztag)
              flashed once to Oki ML67Q4051
```

- **MTL is functional (ML/Lisp-family), NOT Forth.** Only the *VM execution model*
  is stack-based, and Forth is a separate language *implemented in MTL* on top.
  (Several of the source archives mislabel MTL as "Forth-like" — ignore that.)
- **Hardware:** ARM7TDMI, Oki **ML67Q4051**, RT2501 USB WiFi. (Not Cortex-M3.)

So "best of breed" mostly means **stacking** the repos, not choosing between them —
with a few genuine either/or conflicts (§4).

---

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

## 4. Conflicts to resolve (genuine either/or)

### 4.1 Two MTL compilers + simulators ⚔ **biggest decision**
- **`mtl_linux`** (rngtng): used by **nabgcc** (git submodule) **and** `mtl_library`
  (brew-installable). Actively maintained, packaged, already wired into the
  Dockerized Task flow.
- **`nabaztag-piper/compiler/`** (vendored Huet C++ original: `vcomp` + `mtl_comp` +
  `mtl_simu`): self-contained (no submodule), and its **`mtl_simu` has a richer host
  simulator** — fake audio, network, and an HTTP server — which matters for the
  "simu" priority.

→ **DECIDED (evaluated): single-toolchain — `mtl_linux` for BOTH compile and simulate.**
A source-level comparison of the two simulators (both descend from Huet's original)
found they are code-identical or stub except for **one** real capability piper has and
mtl_linux lacks: a ~191-line **HTTP file server** in `http_server.c` (serves `bc.jsp` +
`.forth` from the simulator, with `--http_server_path`/`--http_server_port`). Net/audio
are identical/stubbed; both reference the same `vbc.h` opcode set (one-opcode diff) → no
parity risk. So: **drop the split and the parity spike**; use mtl_linux end-to-end and
**cherry-pick piper's `http_server.c` (~200 LOC) into mtl_linux's simu** — best of both
without forking a second VM.

### 4.2 Two diverged precompilers
`preproc.pl` exists in both `mtl_library/_pre-compiler/` and
`nabaztag-piper/scripts/` and they **differ**. Same job (`#include`/`#define`/`#ifdef`
+ `preproc_remove_extra_protos`). → Merge into one canonical preprocessor; piper's adds
`__DATE__`/revision injection worth keeping.

### 4.3 Two firmware lineages
`firmware_nabaztag` (IAR, original) vs `nabgcc` (GCC port). → `nabgcc` is the live
Layer-0; keep `firmware_nabaztag` only as **provenance/reference** (and to mine the
i2c fix, the redox PSK fix, the WPA2 origin, and the `.sim` packaging cipher). Do not
build from IAR.

### 4.4 Three test/simulate levels — keep all, they're complementary
- `nabgcc/testvm/` — compiles the **C VM** natively, smoke-runs bytecode (catches VM bugs).
- `mtl_simu` — runs an **MTL app** on host with faked HW (catches app-logic bugs).
- `mtl_library/test/` — **MTL assertion framework** run through the simulator (unit tests for stdlib).

→ Adopt all three as distinct SDK test targets: `test-vm`, `simu`, `test-mtl`.

---

## 5. Proposed SDK layout — vendored (no submodules), tooling split from source

**Home = rebooted `NabaztagHackKit` (v2), renamed → `nabaztag-sdk`.** Gut the outdated
Ruby gem + old `bytecode/`; **keep** its `bytecode/_docs/` (grammar/commands) folded into
`docs/`.

**No submodules — copy the sources in.** Vendoring (vs nabgcc's submodule pattern) keeps
the SDK self-contained and low-dependency, at the cost of automatic upstream sync. Since
the upstreams are either frozen (firmware_nabaztag 2021, mtl_library/HackKit 2019) or
yours, that trade is fine. The sync bridge is a **provenance manifest + per-source
CHANGELOG** so fixes can be backported:

```
PROVENANCE.md   # per vendored tree: origin repo + commit SHA + "local changes" list
CHANGELOG.md    # SDK-side changes, tagged by source area, so a diff can be cherry-picked back
```

**Divide *tooling* (how you build) from *source* (what you build), and make every
folder self-contained** — each owns its `Dockerfile` + `Taskfile.yaml`; the root
Taskfile only `includes:` them under a per-layer namespace. (No central `docker/` dir —
that proved to couple unrelated layers.) Compiled binaries are never committed.

```
nabaztag-sdk/   (was NabaztagHackKit — renamed, v2)
├── Taskfile.yaml          # root: just `includes:` each folder's Taskfile (§6)
│
├── tools/                 # ── TOOLING: how you build (not device code) ──
│   ├── mtl_linux/         #   vendored rngtng/mtl_linux: compiler + simulator
│   │   ├── Dockerfile     #     amd64 / 32-bit g++; BUILDS the tools into the image
│   │   └── Taskfile.yaml  #     `mtl:compile` / `mtl:simulate`  (the two verbs)
│   └── pack/              #   Python: mksim, preproc, words, upload, deploy (§7) — later
│       ├── Dockerfile     #     python3 (local)
│       └── Taskfile.yaml  #     `pack:*`
│
├── firmware-c/            # ── SRC, Layer 0: C VM + drivers + WPA2 (from nabgcc) ──
│   ├── Dockerfile         #     arm-none-eabi-gcc + gcc + make (local; no php)
│   └── Taskfile.yaml      #     `fw:*`
├── mtl/                   # ── SRC, Layer 0.5–2: ONE MTL source library (§8) ──
│   ├── lib/               #     forth/ stdlib/ net/ audio/ hw/ chor/ — reusable modules
│   ├── boot.mtl           #     composed entrypoint → Layer 0.5 boot (net+conf+fetch)
│   ├── app.mtl            #     composed entrypoint → Layer 1/2 app (bc.jsp)
│   └── Taskfile.yaml      #     `mtl:*` (uses tools/mtl_linux to compile)
├── forth/                 # ── SRC, Layer 3: vl/ scripts + REPL (apps stripped) ──
│   └── Taskfile.yaml      #     `forth:*`
│
├── docs/                  # grammar/commands (kept) + architecture + generated words.txt
├── PROVENANCE.md          # per vendored tree: origin + commit + local changes
└── reference/             # firmware_nabaztag — read-only provenance, never built
```

> **Built so far:** `tools/mtl_linux/` (compiler + simulator, self-contained, verified).

Discarded from NabaztagHackKit: `Gemfile`, `*.gemspec`, `Rakefile`, `lib/`, `spec/`,
`ext/`, old `bytecode/_original` — superseded by nabgcc / piper / mtl_linux.

Stripped when vendoring nabaztag-piper: `homeassistant/`, `install/piper_tts_stream.py`,
`coqui_cli.py`, `examples/`, `vl/weather.forth` + weather/traffic/pollution scripts.
Kept: the Forth interpreter, `vl/{init,config,hooks,crontab,telnet,consts,palette}.forth`,
build/deploy logic, `scripts/{extract_words,make_nominal}`.

---

## 6. Unified toolchain — Task namespaces per layer

Root `Taskfile.yaml` uses Task's `includes:` to pull a `Taskfile.yaml` from each tree,
so every target is prefixed by its layer. `task --list` then reads as a layer map.

Minimal verbs per namespace — the toolchain build is baked into each image, not a task.

```
mtl:*    ── MTL toolchain: the two verbs (compiler + simulator baked into the image) ──  [BUILT]
  mtl:compile  -- <src.mtl> <out.bin>   compile MTL → bytecode  (boot.mtl → boot, app.mtl → bc.jsp)
  mtl:simulate -- --source <src.mtl>    run an app in the host simulator (+ ported http server, §4.1)
  mtl:test                              MTL assertions in the simulator   (mtl_library/test)   — later
  mtl:words                             regenerate the Forth word ref      (extract_words.py)  — later

fw:*     ── Layer 0: C VM firmware ──
  fw:build          Nab.elf/.hex/.bin                         (nabgcc make)
  fw:test           native C-VM test harness                  (testvm)
  fw:flash:jtag     OpenOCD + GDB over probe  (host openocd)
  fw:flash:sim      package .sim                              (Python mksim, §7)

forth:*  ── Layer 3: runtime scripts ──
  forth:repl        open telnet REPL to a device
  (hot-reload is runtime: `"init.forth" load-srv` over telnet)

dev:*    ── meta / device I/O ──
  dev:deploy        push bc.jsp + *.forth → web server         (T1/T2 OTA)
  dev:upload        POST .sim to the rabbit's config page      (NEW, §7 — kills manual upload)
  dev:docs          docs (grammar/commands + words.txt)
```

(boot vs app is just which entrypoint you pass to `mtl:compile` — §8.) Everything runs in
Docker, so host deps are **Docker + Task** only (plus host `openocd` for JTAG flashing).

---

## 7. Low-dependency stack — settle on Python + C/C++

Today the stack sprawls across **Perl** (preproc), **Ruby** (HackKit gem), **Python**
(piper scripts, fw_nabaztag packaging), **C/C++** (VM + MTL compiler/simu), **PHP**
(nabgcc `mkfirmware.php`). And `.jsp` is **not** Java — `bc.jsp` is just served bytecode
with a misleading extension (a relic of the Violet server). Consolidate to **two**:

| Language | Role | Keep / drop |
|----------|------|-------------|
| **C / C++** | the compiled artifacts: C VM (firmware) + MTL compiler + MTL simulator | **keep** — unavoidable, but compiled once in Docker; not a runtime dep |
| **Python** | all glue tooling: `.sim` packaging, precompiler, word-gen, deploy, upload | **keep — standardize here** |
| Perl | `preproc.pl` | **→ port to Python** (kills Perl from the image) |
| Ruby | HackKit gem | **drop** (gutted on reboot) |
| PHP | `mkfirmware.php` | **→ port to Python** (`firmware_nabaztag/utility/mkfirmware.py` already exists — adopt it; drops `php-cli` from `Dockerfile`) |

Net: **Python is the one scripting language**; C/C++ stays only as Docker-built binaries.
The image loses `php-cli` and `perl`. `.jsp` stays as a filename only (the device's boot
requests that exact path) — rename internally if desired, but the wire path is fixed.

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

## 8. One MTL library, composed into multiple targets

You asked whether to keep single or multiple MTL libs and cherry-pick per target. Answer:
**one source library, multiple composed entrypoints** — the precompiler's
`#include`/`#ifdef` (§4.2) *is* the cherry-pick mechanism.

```
mtl/lib/         forth/  stdlib/  net/  audio/  hw/  chor/  utils/   ← reusable modules
mtl/boot.mtl     #include net, conf, http, config-server, bootstrap   → Layer 0.5  (small)
mtl/app.mtl      #include forth, stdlib, audio, chor, servers, crontab → Layer 1/2  (bc.jsp)
                 #ifdef MONOLITHIC → also embed app into boot for the standalone image
```

- **`boot.mtl`** pulls the minimal set (network bring-up, `conf.bin` access, the config
  web page, and the `bc.jsp` fetch-and-run). This is the modern boot that **neither
  upstream provides buildable** (§3.2) — net-new, but cherry-picked from piper's
  `boot/*.mtl` + `firmware/net`.
- **`app.mtl`** pulls everything else and compiles to `bc.jsp`.
- Same modules feed both; `#ifdef SIMU` / `#ifdef MONOLITHIC` select host-sim shims or
  the standalone embed. No forked copies — one library, build flags choose the slice.

Where do WLAN / proxy / server configs live in this model? **In `conf.bin` on the
device**, written by `boot.mtl`'s config page (or by `dev:upload`, §9) — never in source.
Source owns *behavior*; `conf.bin` owns *per-device settings*.

---

## 9. Net-new SDK work (beyond vendoring) + stability carryover

Vendoring gets you a build; these are the pieces that don't exist yet or need finishing:

1. **Buildable `boot.mtl`** (§3.2/§8) — compose a modern boot from piper's frozen
   `boot/*.mtl` so the remote-load model is fully owned/rebuildable. Biggest new effort.
2. **`dev:upload`** (Python) — POST a `.sim` to the rabbit's on-device config page so
   firmware updates skip the manual browser step. **No such script exists today**, but
   the device side already supports it: `boot.mtl`'s `startconfigserver` + `getbinary` +
   `reboot` (page 3) accept an uploaded image. This is a small, high-value win — automate
   the HTTP POST that the config page performs.
3. **Port `preproc.pl` → Python** and **adopt `mkfirmware.py`** over the PHP (§7).
4. **Port piper's `http_server.c` → mtl_linux's simu** (§4.1) — the one capability gap
   (~200 LOC). The opcode-parity spike is **dropped**: single toolchain end-to-end means
   bytecode never crosses VMs, so there is nothing to keep in sync.

**Stability carried from nabgcc's audit:** critical Layer-0 bugs already fixed (PMK
`strcpy`→`memcpy`, VM array-store `||`→`&&`, USB IRQ re-enable, seeded `rand()`, OTA
bounds). Deferred & documented: AES-128 group-key (`aes128.c` stubs) → WPA2
broadcast/multicast broken, **unicast OK** (ship-with-doc, per decision); bytecode-loader
bounds (trusted in the serverless model — you serve your own `bc.jsp`); `testvm` needs
real assertions.

---

## 10. Decisions (locked)

1. **SDK home** → reboot **NabaztagHackKit as v2**, rename `nabaztag-sdk`, keep its docs,
   gut Ruby/bytecode.
2. **Vendoring, not submodules** → copy sources in; track origin + changes in
   `PROVENANCE.md` + `CHANGELOG.md` for backport. Discontinue standalone `mtl_library`
   and its homebrew tap (or rename the tap → `homebrew-nabaztag-sdk` if a native install
   path is still wanted).
3. **MTL toolchain** → **single toolchain: `mtl_linux` for both compile AND simulate**
   (§4.1, decided after a source-level compare of both simulators). Cherry-pick piper's
   ~200-LOC HTTP server into mtl_linux's simu; no split, no parity spike.
4. **Languages** → Python for all tooling; C/C++ only as Docker-built binaries; drop
   Perl, Ruby, PHP (§7).
5. **Docker + Task** → yes; per-layer namespaces `mtl:/fw:/forth:/dev:` (§6), each folder
   self-contained (owns its `Dockerfile` + `Taskfile`; no central `docker/`).
6. **Embedding** → default remote-load, keep monolithic for recovery (§3.2); one MTL
   library, composed entrypoints (§8).
7. **WPA2** → ship now, document the group-key gap (§9).
8. **Tie-break** → when in doubt, prefer **nabaztag-piper**.

> Status: **reboot underway.** Done: gutted the Ruby gem, vendored `tools/mtl_linux/`
> (compiler + simulator, self-contained, builds in Docker — verified). Next build step:
> the **precompiler** (gates `bc.c` → firmware) — see `TODO.md`.
```
