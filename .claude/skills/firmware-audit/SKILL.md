---
name: firmware-audit
description: Auditing a firmware layer for bugs, inefficiencies or structural problems — inventory the surface, decide what is testable, and prove each finding with a failing test. Use for any "review/inspect/audit the code", "find bugs in", "check for issues in" or "is this ready to extend" request against lua/ or mtl/, and before proposing a refactor of a layer you have not measured.
---

# Auditing a firmware layer

The rule that pays for itself: **inventory the whole surface before going deep
on any of it.** Depth-first exploration guided by intuition finds the shallow
bugs first and the severe ones last, if at all — a review done that way needed
three passes here, and the pre-authentication remote defects surfaced only in
the third.

## 1. Inventory first, depth second

Enumerate every file in the layer and mark each **covered / uncovered /
skipped-with-reason** before reading any of them closely. Only then pick targets.
For `lua/` the surface is:

| Area | Watch for |
|---|---|
| `src/main.c` | carries `main()` — nothing links it, so nothing in it is unit-testable. Biggest structural blocker; see §4 |
| `src/hal/*.c` | unbounded polls, IRQ masking, register field-vs-bit confusion |
| `src/net/`, `src/usb/` | **vendored**, parses attacker-controlled frames — the highest-severity area, and the least obvious to test (§2) |
| `sys/` | startup, linker script, stack sizes, `.ramfunc` placement |
| `lua/` (vendored PUC-Rio) | the local deltas listed in `PROVENANCE.md`, and what dropping the parser did to the input surface |
| `boot/boot.lua` | resident, costs flash, and holds `sched` — the reactor everything composes through |
| `lib/*/*.lua` | protocol parsers, state machines, and whether they are pumpable |
| `tools/*.py`, Taskfiles | build/test tooling — audit it too (§3) |

Two questions that repeatedly found things here:

- **Who can reach this input?** Anything parsing a frame off the radio is
  reachable by anyone in range, unauthenticated, before any key check. That
  outranks everything reachable only from a local REPL.
- **Does the same file already do it right somewhere else?** An inconsistency
  inside one file is an omission, not a design choice, and it is the strongest
  possible evidence — `ieee80211.c` bounded the SSID copy in its scan path and
  not in its AP path.

## 2. Decide what is testable — do not assume

Before writing off a file as "needs hardware", **probe it**:

```sh
gcc -fsyntax-only -std=gnu11 -D_NAB_SIM \
    -Ilua/firmware/inc -Ilua/firmware/test/host/stubs -Ilua/firmware/sys/inc \
    -w lua/firmware/src/net/ieee80211.c
gcc -c ... -o /tmp/x.o <file> && nm -u /tmp/x.o | wc -l
```

Under ~30 undefined symbols, all of them board/driver entry points, means it
links host-native against stubs and runs under ASan. That is how four
code-reading findings became ASan-proven ones. Vendored files count — they are
exempt from `-Werror`, so their rule adds `-w`.

If a register the file touches is missing from `stubs/`, add just that one:
the stub fails to compile the moment a driver reaches for something new, which
is the point.

**What genuinely cannot be host-tested:** `main.c` (see §4), and anything needing
a live peripheral peer. Those go to the simulator or to a hardware checklist.

## 3. Audit the test tooling too

`CLAUDE.md`'s vacuous-pass rule applies to the **runner**, not just the test. A
selector the runner silently ignores reports a pass having run nothing — that
happened here: `SCENARIO=num-large` printed "all checks passed" while executing
a different binary. When you add a scenario, run it and confirm the output names
*your* scenario.

## 4. Structural findings are findings

Report these as defects with the same weight as a memory error:

- **Untestable-by-construction code.** New logic in `main.c` that is not wiring
  belongs in its own TU — `fmt.c` (#245) and `lcframe.c` (#298) were split out
  for exactly this, and every remaining gap in `main.c` blocked a proof.
- **A gate that can never pass.** If a measurement will stay red until a decision
  is taken, make it a **ratchet** against a deterministic baseline (see
  `test/bytecode/run.lua`) and open an issue for the decision. A permanently red
  gate is one people learn to ignore — the vacuous-pass problem running backwards.
- **A documented claim the code does not support.** `README.md` said the
  parser-less image "hardens" the sandbox; dropping `lparser` replaced a front
  end that rejects malformed input by construction with one that does not. Fix
  the text in the same commit as the test.

## 5. Proving a finding

Every finding needs a test that **fails now and passes when fixed**, committed
alongside the passing baselines so it cannot go green vacuously. Match the proof
to the defect:

| Defect shape | Vehicle |
|---|---|
| memory safety in board-independent or stub-able C | `test/host/*.c` under ASan |
| pure-Lua logic (`lib/`, `boot/`) | `lua:lib:test` / `lua:boot:test`, host lua |
| "this loop terminates" | `alarm(2)` + a handler — a hang cannot be asserted on a return value |
| cost / "does too much work" | `debug.sethook` VM-instruction counts, **not** wall clock |
| whole-image behaviour | a simulator golden, guarded by a required marker |
| parser over attacker-controlled input | a **deterministic mutation fuzz** (§5b) |
| needs a real peripheral | a hardware checklist, and say plainly it is unverified |

### 5b. Fuzz the parsers, and make the fuzzer prove itself

For anything walking bytes off the air or off the wire, hand-written fixtures
only cover the cases you thought of — and the whole problem is the ones you did
not. A few hundred lines of deterministic mutation fuzz over the real entry
points is cheap (`ieee80211_test`'s `fuzz`: 20,000 iterations, 46 ms) and it
earns its place twice over: it found the true severity of a defect that had
already been "analysed" by reading it, and it retired a planned refactor by
showing the RSN sub-parse had nothing left in it.

Build it so a clean run means something:

- **Fixed seed.** A failure must reproduce from the scenario name alone, and CI
  must not go red on a Tuesday.
- **Bias the mutations.** Uniform noise mostly produces frames that are rejected
  in the first two bytes. Weight towards boundary values (0, 1, 0x7F, 0x80, 0xFF)
  and, above all, towards **truncation** — every real defect in `ieee80211.c` was
  a length field that outran the buffer.
- **Controls on both sides.** A well-formed input must parse *before* the storm
  and again *after* it. Without the second control, a parser that quietly wedged
  itself — or a harness that stopped delivering — reports a clean run over
  nothing. Assert the iteration count too.
- **Point it at the pre-fix source before you trust it.** A fuzzer that finds
  nothing on known-buggy code is measuring nothing. Ours is kept honest by
  rediscovering both #315 and #317 when built against the commits before them.

### 5c. A stub that models a seam wrongly does not miss defects — it invents them

Model the seam **faithfully, not conveniently**. `nab.on` holds one callback per
name and replaces it silently; a stub that chained them would have hidden the
defect it was written to catch. That is the mild version.

The severe version is #315, and it is worth knowing in detail because the harness
looked fine and the conclusion it produced was inverted. `rt2501_tx` hands its
buffer to the USB stack, which frees it on the URB completion. The stub was:

```c
int8_t rt2501_tx(void *frame, uint32_t len) { tx_calls++; return 1; }
```

Nothing ever released a frame the driver had **correctly** handed away, so a
correct path looked like a leak. A test was then written to pin the "leak" —
`CHECK(free_calls == malloc_calls)` — asserting a contract the device does not
have. The "fix" for the imaginary leak was a double free on real silicon, worse
than the bug it was invented to solve, and the test then defended it. The same
misreading nearly produced a second one on `rt2501_auth`.

So: **a test asserting a contract the hardware does not have is worse than no
test.** When a stub makes a driver look wrong, suspect the stub first —
especially when the "defect" is an omission (something not freed, not called,
not reset) rather than a commission, because an omission is exactly what an
unmodelled hand-off looks like. Before believing it, find the code that would
have done the thing, and check whether your stub stands in for it.

## 6. Before you commit

- `git diff --stat` after any scripted edit. A 60-line fix showing 4,000 changed
  lines means you rewrote the file — see `CLAUDE.md` → Vendoring.
- Run the layer's gate (`task lua:verify`). Re-run `scripts/claude-setup.sh` on
  any `Cannot connect to the Docker daemon`.
- Run Docker commands **from the repo root** — mount paths are repo-relative.
- Say what you did *not* verify. Hardware, other tracks, and skipped scope are
  part of the result.

## 7. Reporting

Keep one findings register with stable IDs from the first pass; issue numbers
and commit messages both refer back to it. Renumbering midway is confusing for
everyone including you.

For each finding record: what breaks, **who can trigger it**, the proof command
and its output, and the blast radius. "13% of corruptions kill the runtime, and
there is no MMU so that is silent heap corruption" lands; "the loader is not
robust" does not.

### State impact only after you have run it

§5 gates the *fix* on a failing test. Gate the **claim** on it too: do not put a
severity in an issue, a comment, a commit message or a PR body until the proof
has executed. Reading the code tells you something is wrong; it does not tell
you how wrong, and this is the mistake this repo keeps paying for.

#317's severity was published three times before it was run once. "Up to 255
bytes out of bounds" — wrong, the consumer was length-checked, which reading the
walk in isolation did not show. Then "one byte, plus 128 if a stray byte happens
to be 128" — wrong the other way, too generous. The truth, once a fuzzer reached
it, was an attacker-controlled 128-byte over-read **that gets transmitted**, a
different class of bug from either guess. Three GitHub corrections and two
rewrites of the same source comment, all avoidable by writing the test first.

Note the direction of the errors: an over-read looks *smaller* when you stop at
the walk and never follow the value into its consumer, and *larger* when you
assume the consumer is unbounded. Neither is conservative, so "I'll err on the
severe side" is not a fix. Running it is.

Until it runs, say what you actually have: **"this looks wrong, here is the probe
I am about to write"** is honest and costs nothing. A severity is a claim about
the device, and only the device — or a faithful harness — can settle it.
