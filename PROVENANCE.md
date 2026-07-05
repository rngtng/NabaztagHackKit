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
