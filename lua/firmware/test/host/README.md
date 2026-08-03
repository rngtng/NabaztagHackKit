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
| `ieee80211_test.c` | `src/net/ieee80211.c` | the probe-request builder must not overrun its frame on an over-long SSID (`nab.wifi`/`nab.wifi_scan` cap nothing), and the **receive** path must survive hostile IE lengths — a probe request's SSID copy, the IE walk's off-by-one, the RSN suite count, and the probe-response frame that is never freed |
| `eapol_test.c` | `src/net/eapol.c` | the WPA2 4-way handshake must bound its reads by the RECEIVED length, not by the lengths the frame declares (`body_length` drives the MIC computation pre-authentication; `key_data_length` drives the GTK unwrap post-MIC) |
| `lcframe_test.c` | `src/utils/lcframe.c` | the `#LC` frame checksum (#298) — pinned vectors so the C receiver and the three Python senders cannot drift, and every single-nibble flip in a chunk-sized buffer detected — plus the header parse (#306): a **refused** header must still report the payload queued behind it, or the console reads bytecode as REPL lines |
| `adc_test.c` | `src/hal/adc.c` | the wheel read must terminate on a converter that never finishes (it feeds the watchdog while it waits, so a hang has no reboot) |

`stubs/ml674061.h` (and `stubs/ml60842.h` for the USB block) shadows the real register map with a plain RAM array, so a
register-only driver runs natively. It models **only** what the drivers under
test touch — deliberately: a stub mirroring the whole 1068-line header would
rot silently against it, whereas this one fails to compile the moment a driver
reaches for something new.

Vendored sources can live here too — `ieee80211_test.c` links `src/net/ieee80211.c` as-is,
which needs only 25 externals, all of them board or driver entry points. Vendored files
are exempt from our `-Werror` set, so their rule adds `-w`.

### Is a file host-testable? Probe, don't guess

"That one needs hardware" is usually wrong. Two commands settle it — run them
**from the repo root**, the paths here are repo-relative:

```sh
gcc -fsyntax-only -std=gnu11 -D_NAB_SIM \
    -Ilua/firmware/inc -Ilua/firmware/test/host/stubs -Ilua/firmware/sys/inc \
    -w lua/firmware/src/net/ieee80211.c

gcc -c -std=gnu11 -D_NAB_SIM -Ilua/firmware/inc \
    -Ilua/firmware/test/host/stubs -Ilua/firmware/sys/inc -w \
    -o /tmp/probe.o lua/firmware/src/net/eapol.c && nm -u /tmp/probe.o
```

Under ~30 undefined symbols, all of them board or driver entry points, means the
file links here against stubs and runs under ASan. Four findings in the #291
review were code-reading suspicions until this probe turned them into ASan-proven
defects. If a register is missing from `stubs/`, add just that one symbol.

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
  `SCENARIO=` alone means `event_test`; name the binary for any other
  (`task lua:firmware:test:host TEST=adc_test SCENARIO=wedged`). An unknown
  scenario name matches nothing and the binary exits 0 — so a `SCENARIO` sent to
  the wrong `TEST` reports a pass having run nothing. Check the pair.
- **A hang is a legitimate assertion.** `adc_test`'s `wedged` scenario arms
  `alarm(2)` and reports the timeout, because "this loop terminates" cannot be
  asserted on the return value of a call that never returns.
- A new test is a new `.c` here plus one line in the `Makefile`'s `TESTS`.
- **Run every command from the repo root**, including a hand-rolled `docker run`.
  The mount paths are repo-relative, so a `$(realpath lua/firmware)` evaluated after
  `cd lua/firmware` resolves to nothing, mounts an empty directory, and reports *every*
  test as FAIL — a failure mode that looks like a real regression.
