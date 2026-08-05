# NabaztagSDK — working conventions

The rules we converged on; apply them by default. Rationale lives in the layer READMEs,
roadmap in GitHub Issues, vendored-source origins in `PROVENANCE.md`. (Global
`~/.claude/CLAUDE.md` still applies.)

**Two independent firmware tracks** ([`README.md`](README.md) has the split): `lua/` —
bare-metal Lua 5.4, the active track — and `mtl/` — the classic C VM + MTL + Forth stack.
**Scope: toolchain only** (build / simulate / test / flash). High-level apps — Home
Assistant, weather, TTS — are out of scope: they consume the SDK, they aren't part of it.

## Session bootstrap

- Run `scripts/claude-setup.sh` once per session (idempotent). Run it *before* a long
  `verify`, not just after one dies — a daemon death mid-run costs the whole run.
- **`Cannot connect to the Docker daemon` means the daemon died, not that you did
  something wrong — just re-run the script.** It happens several times in a long
  session; it is one command, not a diagnosis.
- Host needs only Docker + Task; everything builds in containers. The MTL image is amd64,
  emulated on Apple Silicon, where `cc1` **segfaults nondeterministically** ("internal
  compiler error") — **re-run it.**

## Definition of done

- **`task lua:verify` before every lua commit, `task mtl:verify` before every mtl commit,
  `task verify` for both.** A change that passes one track can still break the other.
- A layer is done when it builds in Docker **and** is committed **and** `PROVENANCE.md` is
  updated **and** the folder has a short `README.md`. Verify by running the task in
  Docker, not by reasoning that it should work. **`task` with no args lists every
  target — it, not any doc, is the authoritative list.**
- Simulator e2e: run the `simulate` task in the background, then
  `curl --noproxy localhost -m 5 localhost:8080/...` (the session's HTTPS proxy otherwise
  intercepts plain `curl localhost`).

## Testing

- **Assert positive expected content, never just that two runs agree.** A golden test
  comparing two stdout captures passes on `empty == empty`. Require a known marker in the
  output before comparing; an all-empty run must fail.
- **That rule covers the runner too.** A selector the runner silently ignores reports a
  pass having run nothing. After adding a scenario, confirm the output names *your*
  scenario — an unmatched selector must fail, not exit 0.
- **New non-wiring logic in `main.c` goes in its own TU.** `main.c` carries `main()`, so
  nothing can link it and nothing in it is unit-testable. It keeps wiring; anything with a
  rule to it gets a file and a `test/host/` test.

## lua track (active)

- **The design principles and the flash budget are binding**, both in
  [`lua/firmware/README.md`](lua/firmware/README.md). A change that breaks a principle
  needs a stated reason.
- **`task lua:firmware:build` fails loudly on flash overflow — believe it.** A new libc
  call can silently cost kilobytes: check the map's **"Archive member"** section,
  `--gc-sections` alone will not tell you.
- **The image is parser-less by design** — the rabbit runs only `luac` bytecode, compiled
  off-device. Source typed at a bare terminal will not run.
- Params: `EXAMPLE=<name>` is a C bring-up program under `firmware/examples/`,
  `APP=<path>` a Lua app source (layer-root-relative). `compile` = Lua→bytecode,
  `build` = binary image.
- JTAG flashing and the UART REPL: use the **`hw-flash-repl` skill**.

## mtl track

Lower-traffic; the detail lives in [`mtl/README.md`](mtl/README.md). Two things to know
before touching it:
- **Its build/simulate tasks and `mtl:lib:test` always exit 0** — the MTL compiler and
  simulator report fatal errors on stderr without failing the process. Both are wrapped to
  turn that into a real nonzero exit, so **trust the exit code; don't grep output by hand.**
- Writing or debugging `.mtl`: the **`mtl-lang` skill**. VM internals and the twin-VM rule
  (patch both copies): [`mtl/tools/testvm/README.md`](mtl/tools/testvm/README.md).

## Structure & vendoring

- **Self-contained layer folders**: each owns its `Dockerfile` + `Taskfile.yaml`; the root
  only `includes:` them. Apply the minimalism rule to **interfaces**, not just code.
- **Copy vendored sources in, don't submodule**; record origin + commit + local changes in
  `PROVENANCE.md`. **Never vendor secrets** — keep the sanitized `*.sample` only.
- **Never reformat a vendored file.** Several are CRLF, and a scripted rewrite turns a
  small fix into a whole-file diff that destroys the backport bridge. `task lua:check:eol`
  gates the `net/` ones; still check `git diff --stat` after any scripted edit.
- **Languages: Python (all glue) + C/C++ (Docker-built binaries).** No Perl/Ruby/PHP.

## Working agreement

Commit per logical change with the `Co-Authored-By` trailer. Keep the layer READMEs and
`PROVENANCE.md` in sync as decisions land. Surface genuine forks as decisions; otherwise
pick the convention above and proceed.
- **A PR that resolves an issue says `Fixes #<n>`** at the top of the body.
- **Multi-milestone arc = one feature branch.** If you must stack, branch off the current
  tip (never `main`) and say so up front.
- **This file is prepended to every session, so it is not free.** Before adding a lesson,
  check whether a task can catch it instead — a gate that fails beats a paragraph that
  hopes. Keep only what a competent contributor would plausibly get wrong, where the
  mistake costs real time and nothing already checks it; put the *why* next to the code it
  protects. When you touch a section, delete what tooling now covers.
