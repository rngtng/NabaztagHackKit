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
| needs a real peripheral | a hardware checklist, and say plainly it is unverified |

Model the seam **faithfully, not conveniently**. `nab.on` holds one callback per
name and replaces it silently; a stub that chained them would have hidden the
defect it was written to catch.

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
