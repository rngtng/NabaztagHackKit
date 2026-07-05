# Provenance

Tracks vendored (copied-in, not submoduled) sources: origin repo + commit + local changes.

## `tools/testvm/`

Vendored from `nabgcc/testvm/` (see `TODO.md` §2, "C-VM unit/smoke test").

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
