# NabaztagSDK — working conventions

Apply these by default; they're the rules we converged on, so I shouldn't need
re-steering. Rationale lives in `NABAZTAG_SDK.md`; roadmap in GitHub Issues; vendored-source
origins in `PROVENANCE.md`. (Global `~/.claude/CLAUDE.md` rules still apply — concise,
`fd`/`rg`, `uv`, prefer Taskfile, commit per change, code minimalism.)

## What this is
A layered SDK for the Nabaztag: **C VM firmware → MTL toolchain → MTL app → Forth scripts**.
This repo is the rebooted **NabaztagHackKit** on branch **`sdk** (upstream
`rngtng/NabaztagHackKit`; the user *is* rngtng — free to rename/restructure their repos).

**Scope:** toolchain only (build / simulate / test / flash-upload). **Exclude** high-level
apps — Home Assistant, weather, TTS. Tie-break when unsure: **prefer `nabaztag-piper`**.

## Build order (bootstrap)
`mtl toolchain → bc.c (compile boot.mtl) → boot/app bytecode → firmware-c → forth`.
The C firmware needs a real `bc.c`, which only exists once the MTL compiler can produce it —
so the **compiler comes before firmware**. Don't start a layer whose inputs aren't built.

## Structure
- **Self-contained layer folders.** Each tool/layer owns its `Dockerfile` + `Taskfile.yaml`.
  The root `Taskfile.yaml` only `includes:` them under a namespace. **No central `docker/`.**
- **Tasks = minimal parameterized verbs** (e.g. `mtl:compile -- <src> <out>`,
  `mtl:simulate -- …`). Bake toolchain *builds* into the image; mark build/image tasks
  `internal: true`. Don't expose one task per Make target.
- Apply the minimalism rule to **interfaces (tasks/CLI/structure), not just code.**

## Docker + Task
- Host needs only **Docker + Task**; everything builds in containers.
- The MTL image is **amd64** (32-bit tools) → emulated on Apple Silicon. `cc1`
  **occasionally segfaults** ("internal compiler error") — it's non-deterministic, **re-run**.

## Openocd / hardware flashing (firmwareV2)
JTAG flashing is the one host-side exception (USB): openocd on a Raspberry Pi
(`ssh tobi@jtag.local`, native `bcm2835gpio` bit-bang, patched openocd 0.8.0).
**The full workflow — flash/REPL tasks, peripheral-existence check, semihosting
breakpoint gotcha, run serialisation, `<<FV_DONE>>` marker, hardware-debugging
discipline — now lives in the `hw-flash-repl` skill.** Invoke it for any
JTAG/flash/console task instead of re-deriving from here; full recipe still in
`tools/openocd/README.md`, board teardown in `docs/hardware-dissection.md`.

## Firmware design principles (firmwareV2)
- **Five binding design principles (#183) live in [`src/firmwareV2/README.md`](src/firmwareV2/README.md#design-principles):**
  (1) layered API — HAL in C, behaviour in Lua, only thin `nab.*` bindings across the seam;
  (2) cooperative event-driven core — never Lua in an ISR; (3) explicit flash/heap +
  error budget — every chunk under `lua_pcall`; (4) partial-update-friendly script slots
  (M11 remote loading, not reflash); (5) sandbox by construction — trimmed stdlib, only the
  `nab` HAL API. **Honour them on new firmwareV2 work; a change that breaks one needs a stated reason.**

## Firmware flash budget (firmwareV2)
- **lua.elf flash budget: ~24.6 KB free of 124 KB after M9+M10 (101,800 B used); was ~30 KB after M8 (#116); ~48 B before M7 (#106).**
  M7 (incl. M7.5 #114) moved Lua's number I/O + console fully off newlib - the
  `luai_*`/printf helpers in `src/app/lua.c` + macro overrides in `lua/luaconf.h`
  Local-config block; the ~10 KB of newlib still linked is soft-float/memcpy/malloc,
  essentially irreducible. Float *printing* stays approximate (integer part + `.0`;
  whole floats correct) pending a real dtoa. **The end-game is measured + decided in #128**:
  wifi C (M11) costs ~26 KB and a resident Lua boot ~12 KB compressed, so the final
  wifi image is **parser-less by design** (−19 KB) plus a compressed bootstrap - the
  rabbit only handles `luac` bytecode; the REPL compiles each line off-device with a
  `LUA_32BITS`-matched host `luac` (flash.py / dev server), and today's `APP=lua`
  stays the dev image - `-Os` (−1.3 KB) and Lua 5.5 (#104) are NOT levers. When
  adding bindings, pick a real lever (see #128), not error-string shaving.
  `task build:firmwareV2 APP=lua` fails loudly on overflow.

## Session bootstrap & verification
- Run `scripts/claude-setup.sh` once per session (idempotent — safe to re-run):
  starts `dockerd` if not running, installs `task` if missing, and — inside the
  Claude Code remote sandbox only — bakes the egress proxy's CA into the
  `python:3.12-slim`/`debian:bookworm-slim` base images so `apt-get`/`pip` inside
  our Dockerfiles can reach the network. No-ops everywhere else.
- **`task build:*` and `task test` always exit 0** — the MTL compiler and simulator
  report fatal errors on stderr but never fail the process. Both are wrapped to
  scan their own output for `Syntax error`/`Typechecking error`/`is EMPTY`/`?OM Error`
  and turn that into a real nonzero exit — trust the exit code, don't grep manually.
- **`task verify` is the definition of done. Run it before every commit** — it
  chains `test` + `build:boot` + `build:app` (app-piper) + `build:app` for
  `src/app-template/main.mtl` (the device-stack build). All four must be green;
  a change that only passes `task test` can still break a build.
- For simulator e2e checks: `task simulate:app`/`simulate:boot` in the background,
  then `curl --noproxy localhost -m 5 localhost:8080/...` (the session's HTTPS
  proxy otherwise intercepts plain `curl localhost`).

## Vendoring (no submodules)
- **Copy sources in**, don't submodule. Record origin repo + commit + local changes in
  `PROVENANCE.md` (the backport bridge).
- **Never vendor secrets.** Strip credential-bearing files (e.g. `conf.bin` → keep only the
  sanitized `*.sample`; git- *and* docker-ignore the real one).
- Exclude build artifacts via `.gitignore` **and** `.dockerignore`; rebuild in Docker.

## Toolchain & languages
- **Single toolchain:** `mtl_linux` is the compiler **and** simulator. Prefer one toolchain
  unless a real capability gap is proven (then cherry-pick the gap, don't fork — e.g. the
  ~200-LOC HTTP server port).
- **Languages: Python (all glue) + C/C++ (Docker-built binaries) only.** No Perl/Ruby/PHP.

## Definition of done (per layer)
Builds in Docker **and** committed **and** `PROVENANCE.md` updated **and** the folder has a
short `README.md`. Verify by actually running the task in Docker — not just "it should work".

## Testing
Harness architecture, stub-ordering rationale, and "how to add a test for a new
lib module" live in `test/README.md` — read it before touching `test/lib/_test.mtl`.

## MTL language gotchas

Compiler parsing/typechecking quirks (`::` precedence, negative-literal
parsing, duplicate-definition binding order, `proto is EMPTY` failures,
simulator heap OOM signatures, etc.) now live in the **`mtl-lang` skill**.
Invoke it when editing an `.mtl` file or chasing a compiler error instead of
re-deriving from here.

## Firmware C VM gotchas
- **The firmware VM and the `mtl_linux` simulator VM are twin copies of the same
  C** (`src/firmware/src/vm/` vs `tools/mtl_linux/src/vm/`). A VM bug or fix almost
  always applies to both — grep the sibling before concluding a divergence is
  intentional, and patch both when fixing.
- **`_bytecode` aliases `_vmem_heap`** (bytecode + heap + downward value stack share
  one `int32_t[VMEM_LENGTH]` array). Stack words are tagged: low bit = pointer,
  else int; a "pointer" is a *word index*, not an address. GC **reboots** on OOM.
- **To exercise the real VM natively**, use `tools/testvm` — build under
  AddressSanitizer via `task test:firmware-bugs`, an ASan **regression guard**
  for the fixed memory-safety bugs (#69/#70) that runs as part of `task verify`;
  every scenario must stay clean. Its `README.md` has the VM internals
  cheat-sheet (value tagging, block layout, how to drive `interpGo()` from a few
  hand-assembled opcodes with no bytecode file). ASan only catches accesses that
  leave the whole heap array, so drive indices past `VMEM_LENGTH` or call helpers
  on separate `malloc`'d buffers.

## Working agreement
Commit per logical change with the `Co-Authored-By` trailer. Keep `NABAZTAG_SDK.md` /
`PROVENANCE.md` in sync as decisions land (roadmap → GitHub Issues). Surface genuine forks as decisions;
otherwise pick the convention above and proceed.
- **A PR that resolves an issue must say `Fixes #<n>` in its body** (GitHub closing
  keyword — `Fixes`/`Closes`/`Resolves`) so the issue auto-closes on merge. One line, at
  the top of the body.
- **Multi-milestone arc = one feature branch**, not a fresh branch per milestone. If
  you must stack, branch off the current tip (never off `main`) and say so up front.
