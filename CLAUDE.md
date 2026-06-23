# NabaztagSDK — working conventions

Apply these by default; they're the rules we converged on, so I shouldn't need
re-steering. Rationale lives in `NABAZTAG_SDK.md`; roadmap in `TODO.md`; vendored-source
origins in `PROVENANCE.md`. (Global `~/.claude/CLAUDE.md` rules still apply — concise,
`fd`/`rg`, `uv`, prefer Taskfile, commit per change, code minimalism.)

## What this is
A layered SDK for the Nabaztag: **C VM firmware → MTL toolchain → MTL app → Forth scripts**.
This repo is the rebooted **NabaztagHackKit** on branch **`v2`** (upstream
`rngtng/NabaztagHackKit`; the user *is* rngtng — free to rename/restructure their repos).
Not pushed yet.

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
- JTAG flashing is the one host-side exception (USB).

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

## Working agreement
Commit per logical change with the `Co-Authored-By` trailer. Keep `NABAZTAG_SDK.md` /
`TODO.md` / `PROVENANCE.md` in sync as decisions land. Surface genuine forks as decisions;
otherwise pick the convention above and proceed.
