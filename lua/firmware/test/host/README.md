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

Anything that is either board-independent, or register-only and therefore
coverable by the fake register map in `stubs/ml674061.h`. Today:

| Test | Source under test | Guards |
|---|---|---|
| `event_test.c` | `src/utils/event.c` | #242 — a dropped `event_post()` must not lose the edge permanently |
| `fmt_test.c` | `src/utils/fmt.c` | #245 (no stray NUL on stderr), #254 (`fmt_hex8`), the `num-large` digit-buffer bound, plus first-ever coverage of the hand-rolled `vsnprintf` |
| `rfid_test.c` | `src/hal/rfid.c` | #253 — a wedged bus must fail fast, not retry for minutes |
| `i2c_test.c` | `src/hal/i2c.c` | #246 (mask nesting), #252 (polls must not run masked) |

`stubs/ml674061.h` shadows the real register map with a plain RAM array, so a
register-only driver runs natively. It models **only** what the drivers under
test touch — deliberately: a stub mirroring the whole 1068-line header would
rot silently against it, whereas this one fails to compile the moment a driver
reaches for something new.

What still can't live here: code inside `main.c` (it carries `main()`, so
nothing else can link it — this is why the printf shims were split out to
`src/utils/fmt.c` in #245), and anything needing a live peripheral peer. Those
stay sim-tested (`firmware:test`, `firmware:test:inject`) or hardware-tested.

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
