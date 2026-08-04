# NabaztagSDK — working conventions

Apply these by default; they're the rules we converged on, so I shouldn't need
re-steering. Rationale lives in `NABAZTAG_SDK.md`; roadmap in GitHub Issues; vendored-source
origins in `PROVENANCE.md`. (Global `~/.claude/CLAUDE.md` rules still apply — concise,
`fd`/`rg`, `uv`, prefer Taskfile, commit per change, code minimalism.)

## What this is
An SDK for the Nabaztag, split into two independent firmware tracks (see #188):
- **`mtl/`** — the classic stack: **C VM firmware → MTL toolchain → MTL app → Forth scripts**.
- **`lua/`** — a re-architecture: bare-metal PUC-Rio Lua 5.4 replacing the VM.

Board revision (`LLC2_2`/`LLC2_3`/`LLC2_4c`) is a separate axis inside the `mtl` track.
Upstream `rngtng/NabaztagHackKit`; the user *is* rngtng — free to rename/restructure their repos.

**Scope:** toolchain only (build / simulate / test / flash-upload). **Exclude** high-level
apps — Home Assistant, weather, TTS. Tie-break when unsure: **prefer `nabaztag-piper`**.

## Build order (bootstrap)
`mtl toolchain → bc.c (compile boot.mtl) → boot/app bytecode → firmware-c → forth`.
The C firmware needs a real `bc.c`, which only exists once the MTL compiler can produce it —
so the **compiler comes before firmware**. Don't start a layer whose inputs aren't built.

## Structure
- **Self-contained layer folders.** Each tool/layer owns its `Dockerfile` + `Taskfile.yaml`.
  The root `Taskfile.yaml` only `includes:` them under a namespace. **No central `docker/`.**
- **Tasks = minimal parameterized verbs** (e.g. `mtl:app-piper:build`,
  `mtl:boot:simulate`, `lua:firmware:flash EXAMPLE=…`). Bake toolchain *builds* into the image;
  mark build/image tasks `internal: true`. Don't expose one task per Make target.
- Apply the minimalism rule to **interfaces (tasks/CLI/structure), not just code.**

## Docker + Task
- Host needs only **Docker + Task**; everything builds in containers.
- The MTL image is **amd64** (32-bit tools) → emulated on Apple Silicon. `cc1`
  **occasionally segfaults** ("internal compiler error") — it's non-deterministic, **re-run**.

## Openocd / hardware flashing (lua track)
JTAG flashing is the one host-side exception (USB): openocd on a Raspberry Pi
(`ssh tobi@jtag.local`, native `bcm2835gpio` bit-bang, patched openocd 0.8.0).
**The console is UART0 @115200 8N1, bidirectional (#203/#207): `print()`/prompt out,
REPL input in, read/driven on the Pi's `/dev/serial0` — no OpenOCD session, no CPU
halts. Drive it with `task lua:firmware:flash:repl`: omit SCRIPT for a live interactive
prompt (`luash.py` compiles each typed line off-device + drives `uart_repl.py --relay`),
or SCRIPT=file.lua|.lc for a scripted run. All input is luac bytecode - the firmware has
no parser (#128); source typed at a bare terminal will not run. Input is paced for flow
control (16-byte RX FIFO, no HW flow control) and ended with EOT (0x04).**
The full workflow — flash/REPL tasks, UART read commands, peripheral-existence check,
run serialisation, `<<FV_DONE>>` marker, hardware-debugging discipline — lives in the
**`hw-flash-repl` skill**. Invoke it for any JTAG/flash/console task; full recipe in
`lua/tools/openocd/README.md`, teardown in `docs/hardware-dissection.md`.
- **Before a hardware round-trip, trace the full runtime path (entry → app logic) to the
  thing you're testing — not just the subsystem you changed.** #207 lost ~6-8 flashes
  chasing "no REPL output" that was `DEMO`'s `run()` looping until a button press, plainly
  readable in `main()`. Extends "read source before iterating" from the transport to the
  whole path to what you're observing.

## Firmware design principles (lua track)
- **Five binding design principles (#183) live in [`lua/firmware/README.md`](lua/firmware/README.md#design-principles):**
  (1) layered API — HAL in C, behaviour in Lua, only thin `nab.*` bindings across the seam;
  (2) cooperative event-driven core — never Lua in an ISR; (3) explicit flash/heap +
  error budget — every chunk under `lua_pcall`; (4) partial-update-friendly script slots
  (remote loading, not reflash); (5) sandbox by construction — trimmed stdlib, only the
  `nab` HAL API. **Honour them on new lua-track work; a change that breaks one needs a stated reason.**

## Firmware flash budget (lua track)
**Numbers live in one place: [`lua/firmware/README.md`](lua/firmware/README.md#flash-budget).**
Don't restate them here - two copies drift. The rules that bind new work:
- **`task lua:firmware:build` fails loudly on overflow.** Believe it; the budget is real.
- **A new libc call can silently cost kilobytes.** `rand()` once re-linked newlib's whole
  stdio FILE layer via `rand -> assert -> fiprintf` (~9 KB, undoing the #106 win);
  `src/utils/libc_shim.c` reclaims it. Check the map's **"Archive member"** section after
  any change that adds one - `--gc-sections` alone will not tell you.
- **No float crosses varargs on the device** (#213): C default argument promotion turns it
  into a `double` and links libgcc's double soft-float. Print floats through the
  non-variadic `luai_num2str`. Float printing stays approximate pending a real dtoa.
- **The image is parser-less by design (#128, done)** - `lparser`/`llex`/`lcode` dropped, the
  rabbit runs ONLY `luac` bytecode, all Lua compiled off-device by a `LUA_32BITS`-matched
  host `luac`. This is the single image; it supersedes #128's dev/prod split.
- **`-Os` and Lua 5.5 are NOT levers**, and neither is error-string shaving. The two cheapest
  real ones are demo assets (the `nab.tone()` MP3 and the resident boot chunk) - **product
  decisions, so ask before spending them.**
- **New blocking bindings must pump the reactor** (#283). `nab.wait`/`nab.delay` (one and the
  same now), `nab.play` and `nab.wifi_recv` all run the event pump + `nab.on("tick")` slice
  while they wait; a binding that spins without doing so reopens the hole where no event is
  sampled, no ear stops on target and no connection is pumped. `task lua:firmware:test:sched`
  is the gate.

## Lua track task surface & structure (post-#208)
- **Leaves, each owning its targets; others delegate (interfaces stay abstract):**
  `boot/` (`compile`) · `apps/` (`compile`,`simulate`,`clean`) · `firmware/`
  (`build`,`simulate`,`simulate:repl`,`flash`,`flash:repl`,`test`,`clean`) ·
  `lib/` (`test`,`size`) · `tools/{luac,simulator}` (image builders) · root
  (`verify`,`clean`,`check:docs`). Delegations: `apps:compile`→`luac:compile`,
  `firmware:build`→`boot:compile`, `firmware:simulate`→`simulator:run`. Inline (can't
  delegate, documented): `apps:simulate` (mid-shell temp frame), `firmware:test`
  (stdout capture), `*:repl` (luash wraps the docker argv), `lib:test/size`.
- **Dependency graph is a downward DAG, no cycle:** `apps → firmware → {luac,
  simulator, boot}`, `boot → luac`, `lib → luac`. A leaf never includes `apps` or root.
- **Param conventions (one meaning each):** `EXAMPLE=<name>` a C bring-up prog under
  `firmware/examples/`; `APP=<path>` a Lua app source, **layer-root-relative**
  (`apps/foo.lua`), same in `apps:compile`/`apps:simulate`; the product firmware needs
  no selector. Verbs: **`compile` = Lua→bytecode** (`apps:compile`,`boot:compile`);
  **`build` = binary image** (`firmware:build`). REPL is a sub-verb of its transport
  (`flash:repl` = HW, `simulate:repl` = sim).
- **Tool image names live ONLY in the tool that builds them, via a UNIQUE var
  (`LUAC_IMAGE`/`SIM_IMAGE`), never a generic `IMAGE`, and no include passes them.**
  This is load-bearing, not style: `tools/luac` + `tools/simulator` get co-included by
  `firmware`/`apps`, and a shared `IMAGE`/`DIR` var **bleeds and collides** across the
  two (Task keeps one entry per leaf-global name) — it once tagged the *simulator*
  build as `nabaztag-sdk-luac`, so `embed.py` ran `simulate.py`. Don't reintroduce a
  generic `IMAGE` or thread image names through `vars:` in includes.
- **`task lua:check:docs`** lints that every `` `task lua:…` `` string in docs/comments/
  skills names a real task (chained into `lua:verify`, fail-fast) — run it after any
  task rename so no doc reference is left dangling.

## Session bootstrap & verification
- Run `scripts/claude-setup.sh` once per session (idempotent — safe to re-run):
  starts `dockerd` if not running, installs `task` if missing, and — inside the
  Claude Code remote sandbox only — bakes the egress proxy's CA into the
  `python:3.12-slim`/`debian:bookworm-slim` base images so `apt-get`/`pip` inside
  our Dockerfiles can reach the network. No-ops everywhere else.
  **`Cannot connect to the Docker daemon` means the daemon died, not that the
  setup was wrong — just re-run the script.** It happens several times in a long
  session; it is one command, not a diagnosis. **Run it *before* a long
  `verify`, not only after one dies** — the script is idempotent and takes a
  second, and a daemon death eight minutes into `lua:verify` costs the whole run.
  Three of seven verify runs in one session were lost to exactly that.
- **The MTL build/simulate tasks (`mtl:<layer>:build`/`:simulate`) and `task mtl:lib:test` always
  exit 0** — the MTL compiler and simulator report fatal errors on stderr but never fail the
  process. Both are wrapped to scan their own output for
  `Syntax error`/`Typechecking error`/`is EMPTY`/`?OM Error` and turn that into a real nonzero
  exit — trust the exit code, don't grep manually.
- **`task mtl:verify` is the definition of done for the mtl track. Run before every mtl commit** —
  chains `mtl:app-{piper,template,sse}:build` + `mtl:boot:build` + `mtl:bootV2:build` +
  `mtl:lib:test` + `mtl:firmware:test` (vm smoke + ASan bugs + crypto KAT). All must be green.
- **`task lua:verify` is the definition of done for the lua track. Run before every lua
  commit** — chains `check:docs` + `firmware:test:host` + `boot:test` +
  `firmware:test:bytecode` + `lib:test` + `firmware:build` + `firmware:test{,:desync,:inject,:sched}`.
- **`task verify` runs both tracks** (`mtl:verify` + `lua:verify`). A change that only passes
  `mtl:lib:test` can still break a build.
- For simulator e2e checks: `task mtl:<app>:simulate`/`mtl:boot:simulate` in the background,
  then `curl --noproxy localhost -m 5 localhost:8080/...` (the session's HTTPS
  proxy otherwise intercepts plain `curl localhost`).

## Vendoring (no submodules)
- **Copy sources in**, don't submodule. Record origin repo + commit + local changes in
  `PROVENANCE.md` (the backport bridge).
- **Never vendor secrets.** Strip credential-bearing files (e.g. `conf.bin` → keep only the
  sanitized `*.sample`; git- *and* docker-ignore the real one).
- Exclude build artifacts via `.gitignore` **and** `.dockerignore`; rebuild in Docker.
- **Never reformat a vendored file — preserve its line endings.** The `mtl/` and `lua/`
  copies of `net/ieee80211.c` and `net/eapol.c` are **CRLF**; Python's
  `read_text()`/`write_text()` silently rewrites them as LF, which turns a 167-line fix
  into a 6,109-line diff and destroys the backport bridge. Edit vendored files in place,
  or in Python go through `read_bytes()`/`write_bytes()`. **Check `git diff --stat` after
  any scripted edit** — a small change with a huge line count means you rewrote the file.

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
lib module" live in `mtl/test/README.md` — read it before touching `mtl/test/lib/_test.mtl`.
- **Golden / round-trip tests must assert positive expected content**, not just that two
  runs match. `firmware:test` (the luac golden) compared source-run vs bytecode-run stdout with `[ "$A" = "$B" ]`
  and passed on `empty == empty` when the app hung before the REPL — a green test that
  validated nothing (#207). Require a known marker in the output before comparing; an
  all-empty run must fail.
- **The vacuous-pass rule covers the runner, not just the test.** A selector the runner
  silently ignores reports a pass having run nothing — `SCENARIO=num-large` sent to the
  wrong `TEST` printed "all checks passed" while executing a different binary. After
  adding a scenario, run it and confirm the output names *your* scenario; an unmatched
  selector should fail, not exit 0.
- **New non-wiring logic in `main.c` goes in its own TU.** `main.c` carries `main()`, so
  nothing can link it and nothing in it is unit-testable — that is why `fmt.c` (#245) and
  `lcframe.c` (#298) were split out. `main.c` keeps wiring; anything with a rule to it
  gets a file and a `test/host/` test.
- **`task lua:firmware:check` before spending a `verify` on a firmware TU.** Nothing links
  `main.c`, so no host test compiles it either: the ~6-minute ARM build is the *only*
  thing that reads it, and it is `-Werror`. A one-line comment edit that left the tail of
  the old comment dangling cost a full verify cycle to discover. The task syntax-checks
  one TU with the real cross-compiler and the Makefile's own `-I` set, in seconds —
  defaults to `src/main.c`, `SRC=` picks another. Needs a prior `firmware:build` for
  `gen/boot_lc.h`, and it says so rather than failing obscurely.

## MTL language gotchas

Compiler parsing/typechecking quirks (`::` precedence, negative-literal
parsing, duplicate-definition binding order, `proto is EMPTY` failures,
simulator heap OOM signatures, etc.) now live in the **`mtl-lang` skill**.
Invoke it when editing an `.mtl` file or chasing a compiler error instead of
re-deriving from here.

## Firmware C VM gotchas (mtl track)
- **The firmware VM and the `mtl_linux` simulator VM are twin copies of the same
  C** (`mtl/firmware/src/vm/` vs `mtl/tools/mtl_linux/src/vm/`). A VM bug or fix almost
  always applies to both — grep the sibling before concluding a divergence is
  intentional, and patch both when fixing.
- **`_bytecode` aliases `_vmem_heap`** (bytecode + heap + downward value stack share
  one `int32_t[VMEM_LENGTH]` array). Stack words are tagged: low bit = pointer,
  else int; a "pointer" is a *word index*, not an address. GC **reboots** on OOM.
- **To exercise the real VM natively**, use `mtl/tools/testvm` — build under
  AddressSanitizer via `task mtl:firmware:test:bugs`, an ASan **regression guard**
  for the fixed memory-safety bugs (#69/#70) that runs as part of `task mtl:verify`;
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
