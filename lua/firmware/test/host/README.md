# Host-side C unit tests

Native (x86/arm64 host, **not** cross-compiled) tests for the firmware's
board-independent C. Run with `task lua:firmware:test:host`; part of
`task lua:verify`.

The lua track's equivalent of `mtl/tools/testvm`'s `bugrepro`: the **real
firmware source is under test**, only the board layer is stubbed. Everything
runs under AddressSanitizer + UBSan, so a memory-safety regression aborts with
a precise report rather than corrupting silently.

## Why host-native

The defects these guard are timing- and capacity-dependent (a queue that
overflows, a bus that wedges). On hardware they need a rare coincidence to
reproduce; here the stubs own the clock and the peripheral answers, so each is
a deterministic, single-digit-millisecond test.

## What can live here

Only sources with no `ml674061.h` register dependency. Today:

| Test | Source under test | Guards |
|---|---|---|
| `event_test.c` | `src/utils/event.c` | #242 — a dropped `event_post()` must not lose the edge permanently |

Anything touching the register macros can't compile for the host and stays
sim-tested (`firmware:test`, `firmware:test:inject`) or hardware-tested.

## Conventions

- **Assert positive expected content**, never just "two runs agree" — the #207
  lesson. `event_test.c` asserts the exact UID bytes come back, not that an
  event arrived.
- **Guard against vacuous passes.** `event_test.c`'s `constants` scenario
  proves the queue really does fill at `EVQ_LEN` and reject the next post; if
  `EVQ_LEN` grew, the other scenarios would stop filling the queue and pass
  while testing nothing.
- **One scenario per `argv[1]`**, so an ASan abort in one can't mask the rest
  (`./event_test rfid-drop`, or `task lua:firmware:test:host SCENARIO=rfid-drop`).
- A new test is a new `.c` here plus one line in the `Makefile`'s `TESTS`.
