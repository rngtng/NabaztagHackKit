# Provenance

Sources are **vendored** (copied, not submodules — see [NABAZTAG_SDK.md](NABAZTAG_SDK.md)
§5). Each entry records origin repo + commit so changes can be diffed and backported
upstream. "Local changes" lists everything we altered from the vendored copy.

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

## Where each part came from

| Path | Origin | Notes |
|---|---|---|
| `tools/mtl_linux/` | `rngtng/mtl_linux` (dockerized in `9037084`) | MTL compiler + simulator (Linux/macOS port of Sylvain Huet's original toolchain) |
| `tools/preprocessor/` | written for this repo | pcpp-based `#include`/`#ifdef` preprocessor replacing piper's Perl one |
| `src/firmware/` | `nabgcc` fork (`2894846`, dockerized `ed3972c`) | C bytecode VM + drivers ported to `arm-none-eabi-gcc`; WPA2 branch |
| `src/boot/` | the original Violet/IAR boot (via `firmware_nabaztag`), split into modules | frozen recovery path — WiFi provisioning + `bc.jsp` fetch; not rebuilt from lib |
| `src/app-piper/` (business-logic layers: srv/, run/, chor/ protocol layers, config, app forth words) | `nabaztag-piper` (ServerlessNabaztag fork), added `3ccbf2d` | the app; most of its former bulk is now `lib/` (see below) |
| `lib/net/`, `lib/hw/`, `lib/audio/`, `lib/chor/` engine, `lib/forth/` word packs | extracted from `nabaztag-piper`'s `src/app-piper/{net,ipv4,hw,audio,chor}` | generic building blocks pulled out behind seams — see `lib/README.md` for the seam contract and CHANGELOG v0.4.0–v0.9.0 for the extraction history |
| `lib/forth/` interpreter core (interpreter, stack/arithmetic/comparison/logical/string/list/control/compile, JSON parser) | nabaztag-piper's Forth interpreter | Copyright (c) 2025 Andrea Bonomi, MIT License (see file headers) |
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
