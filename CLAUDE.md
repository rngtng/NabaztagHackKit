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
- JTAG flashing is the one host-side exception (USB). hence openocd on a raspberry Pi
  (`ssh tobi@jtag.local`, native `bcm2835gpio` bit-bang, patched openocd 0.8.0).
- **Flash a firmwareV2 app with `task flash:firmwareV2 [APP=…]`**; **read its
  console (print/REPL) with `task repl:firmwareV2:hw [APP=… SCRIPT=…]`** - never
  hand-roll scp+openocd+gdb (the raw ssh+openocd path gets denied, and both are
  now taskified). IDCODE `0x3f0f0f0f` on connect = CPU alive / wiring good.
- **Confirm a peripheral EXISTS before writing a driver/binding for it.** Read
  `docs/hardware-dissection.md` (the board teardown / chip list) first, and if the
  rig is up, probe it (e.g. read a device id over the bus) BEFORE building. A wired
  chip-select pin is NOT proof of a populated chip - M6 (#94) built a whole AT45
  flash driver against a `CS_FLASH` pin for a chip the LLC2_4c board doesn't have,
  then had to revert. The cheapest disqualifying test comes first, not last.
- **Semihosting console (M3+)**: the ARM7TDMI EmbeddedICE is **v1, no vector catch**,
  so openocd's auto soft-bp at the SWI vector `0x8` fails (flash is read-only there).
  Must use a **HW breakpoint at 0x8** + `rbp 0x8`; set it AFTER the final `reset halt`
  (reset wipes the ICE watchpoint regs). `task repl:firmwareV2:hw` does this for you;
  full recipe in `tools/openocd/README.md`. Cleaner fix still TODO: patch openocd
  `arm7_9_setup_semihosting` to `BKPT_HARD`.
- **Read the openocd/VM source before iterating on hardware** - a semihosting/flash
  hunch is cheap to confirm in `~/nabgcc/openocd-0.8.0/src` on the Pi; each hardware
  round-trip (flash 124 KB ≈ 13 s, per-char semihosting) is not. Predict, then test.
  This extends to **bus/peripheral semantics** (SPI FIFO, DREQ, chip-select), not
  just semihosting/flash - M8 (#116) burned ~7 round-trips on a silent beep whose
  cause (SPI0 RX-FIFO never drained by `WriteSPI`; DREQ dips after SCI writes) was
  readable in `src/hal/audio.c` + `spi.c` up front. See [[nabaztag-spi0-audio-gotchas]].
- **When a binding fails on hardware but a standalone probe works, instrument the
  *real* failing path in situ - do NOT build ever-closer probe *replicas*.** The
  replica diverges (M8's sine probe was missing `init_spi`) and sends you chasing
  red herrings; a few `sh_puts`/readbacks inside the actual binding localised the
  M8 bug in one run after several wasted on LED/button theories.
- **When a probe works and the full app does not, the *difference* is the bug -
  isolate it, don't tweak the app by-ear.** Take the working minimal probe and add
  the app's factors back one at a time (init order, ExtRAM heap, semihosting I/O),
  reading objective status at each step. #123 wasted ~15 by-ear round-trips guessing
  one VS1003 register at a time; the fix came only once `volprobe` re-added init_hw,
  then a `SYS_READC` burst, then an ExtRAM write burst with `CLOCKF`/`HDAT1`
  readbacks - isolating that **ExtRAM/EMC access corrupts SPI0 SCI**, which the Lua
  heap (in ExtRAM) triggers constantly (see [[nabaztag-emc-spi0-audio-conflict]]).
- **Prefer objective on-device signals over by-ear/by-eye; checkpoint after ~3
  failed same-symptom round-trips.** Every #123 breakthrough was a register/HDAT1/DREQ
  readback; the ear was slow, needed the user, and even gave self-contradictory
  results. After a few failed iterations on one symptom, stop and write
  knowns/unknowns/next-experiment rather than continuing to guess.
- **Get the peripheral datasheet, and diff FW1 (`src/firmware`, the working oracle),
  before guessing a chip's registers.** Most #123 dead-ends - `SM_SDISHARE`
  (shared-chip-select decode mode), VS1003 decodes MP3 not raw PCM WAV, endFillByte,
  `CLOCKF` encoding + post-reset timing - were plain datasheet facts; with no VS10xx
  datasheet in-repo I guessed them one costly round-trip at a time.
- **Serialise hardware runs.** Only one OpenOCD may own the Pi/JTAG at a time -
  launching a second `flash`/`repl:hw` while one is live gives `Error: couldn't
  bind to socket: Address already in use` and a wasted run. Wait for the prior
  background run to finish before starting the next.
- **Semihosting apps print `<<FV_DONE>>` when finished** (the REPL after input EOF,
  a probe before it idles); `flash.py` streams the console live and early-exits on
  that marker instead of waiting out `--run-timeout` (~120 s → seconds). New probe
  apps should emit it before their idle loop.
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

- **Compiler error line = closing bracket of enclosing function**, not offending line.
  Binary-search by commenting out test files to isolate fast.
- **`::` (cons) has higher precedence than function application.**
  `f x :: y :: nil` = `f (x :: y :: nil)`, not `(f x) :: y :: nil`.
- **Negative literals in non-first arg position parse as binary minus.**
  `fun "v" -1` = `(fun "v") - 1`. Use `(0-1)` to force negative integer.
- **`strcmp` returns sign only** (-1/0/1), not character difference like C stdlib.
- **`assert_equalS` breaks on strings containing null bytes** (`\0`).
  Use `strget` to check individual bytes instead.
- **No local function definitions** (`let fun` is invalid). Hoist to top level.
- **`if-then` without `else`** leaves the false branch typed as `I` (0).
  Always add `else nil` when returning a list.
- **Duplicate `fun` definitions are legal; call sites bind the most recent
  definition at their point of compilation.** Later redefinitions do NOT rebind
  earlier call sites (piper's `tcpudp_emu.mtl` override relies on this). Put
  overrides/stubs BEFORE their consumers' includes.
- **Duplicate definitions still share one type.** The checker unifies the
  signatures of all definitions of a name — a stub must be type-compatible
  with the real implementation (e.g. can't pass `I` where the real one takes `Tcp`).
- **`(expr).field` doesn't parse as a function argument** — bind it first:
  `let sock.sockCnx -> cnx in f cnx.field`.
- **Every `proto` needs a real definition somewhere in the compiled program**,
  or the compiler fails late and unhelpfully: `<name> is EMPTY !!!` with no file/line.
  When isolating a module for a test (see `test/README.md`), stub every proto it
  declares, not just the ones you expect to be called.
- **The simulator heap is small (~200K words)** — `#GC` lines after a healthy run
  show low `used=NN%`; a jump to `used=99%` followed by `?OM Error` means a runaway
  allocation, almost always either an infinite loop (e.g. a value-consuming parser
  branch that doesn't advance its cursor) or rebuilding a big structure (a
  dictionary, a table) on every call instead of once at init.

## Firmware C VM gotchas
- **The firmware VM and the `mtl_linux` simulator VM are twin copies of the same
  C** (`src/firmware/src/vm/` vs `tools/mtl_linux/src/vm/`). A VM bug or fix almost
  always applies to both — grep the sibling before concluding a divergence is
  intentional, and patch both when fixing.
- **`_bytecode` aliases `_vmem_heap`** (bytecode + heap + downward value stack share
  one `int32_t[VMEM_LENGTH]` array). Stack words are tagged: low bit = pointer,
  else int; a "pointer" is a *word index*, not an address. GC **reboots** on OOM.
- **To exercise the real VM natively**, use `tools/testvm` — build under
  AddressSanitizer via `task test:firmware-bugs`. Its `README.md` has the VM
  internals cheat-sheet (value tagging, block layout, how to drive `interpGo()`
  from a few hand-assembled opcodes with no bytecode file). ASan only catches
  accesses that leave the whole heap array, so drive indices past `VMEM_LENGTH`
  or call helpers on separate `malloc`'d buffers.

## Working agreement
Commit per logical change with the `Co-Authored-By` trailer. Keep `NABAZTAG_SDK.md` /
`PROVENANCE.md` in sync as decisions land (roadmap → GitHub Issues). Surface genuine forks as decisions;
otherwise pick the convention above and proceed.
- **Multi-milestone arc = one feature branch**, not a fresh branch per milestone. If
  you must stack, branch off the current tip (never off `main`) and say so up front.
