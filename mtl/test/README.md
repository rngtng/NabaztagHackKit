# Test harness

`task mtl:lib:test` preprocesses `test/test.mtl` and runs it in the MTL
simulator. `test/test.mtl` includes `test/_helper.mtl` + `test/_assertions.mtl`,
then `test/lib/_test.mtl` (all of `lib/`) and `test/native/_test.mtl` (VM-native
primitives, no `lib/` dependency).

## Why include order matters in `test/lib/_test.mtl`

MTL lets you redefine a `fun`, but **a call site binds whatever definition of
that name was most recent at its point of compilation** — a later redefinition
does not retroactively rebind earlier callers (see `CLAUDE.md`'s MTL gotchas).
`test/lib/_test.mtl` is built around that rule: every stub is declared
**before** the lib module that calls it, and later stub blocks intentionally
shadow earlier real implementations. Top to bottom:

1. `lib/std/*` — no stubs needed, pure data.
2. `config_get_city_code`/`config_get_dst` stubs, **then** `lib/sys/time.mtl`
   and `lib/hw/*` (ears/leds/etc. depend on time and on each other).
3. The `lib/net` client transport/config stubs (`tcp_open`, `udp_send`,
   `regudp`, `config_get_net_dns`, …), **then** `lib/net/{dns,ntp,http}.mtl`.
   These are included *without* `lib/net/net.mtl` or the ipv4 stack — including
   the real stack here would bind `sock`/`http_server` (compiled later) to the
   ipv4 `writetcp` aliases and silently bypass the capturing TCP stubs below.
   To test something inside the ipv4 stack itself, write an isolated test
   program instead of extending this shared file.
4. `lib/audio`, `lib/chor` — depend on hw/leds, hw/ears.
5. The **capturing TCP stubs** (`writetcp` appends to `test_tcp_out`,
   `test_tcp_partial` simulates short/partial writes, `closetcp` sets
   `test_tcp_closed`), **then** `lib/net/sock.mtl`, `http_server.mtl`,
   `sse_server.mtl` — these bind to the capturing stubs, not the client-test
   stubs from step 3 (different names: `writetcp` vs `tcp_write`).
6. `lib/forth.mtl` + `forth_run`/`forth_run_stack` helpers, which build the
   dictionary **once** — rebuilding it per test run exhausts the simulator's
   ~200K-word heap (the `?OM Error` gotcha in `CLAUDE.md`).

`fun lib_test=` at the bottom is a flat list of `#include`s, one per
`<module>_test.mtl`, each wrapped as `let scenario "name" -> s in (...)`.

## Adding a test for a new lib module

1. Create `test/lib/<subdir>/<module>_test.mtl` mirroring the module's path
   under `lib/` (e.g. `lib/hw/foo.mtl` → `test/lib/hw/foo_test.mtl`).
2. If the module declares `proto` seams, add stubs for **all** of them to
   `test/lib/_test.mtl` before its `#include`, even ones you don't call —
   an undefined proto is a hard compile failure (`X is EMPTY !!!`).
3. Add one `#include "test/lib/<subdir>/<module>_test.mtl"` line inside
   `fun lib_test=`.
4. Run `task mtl:lib:test`; iterate against real assertion failures (`!!`
   lines) — the harness fails the process now (see `CLAUDE.md`).

## Assertions

`assert_equalI`, `assert_equalS`, `assert_nil`, `assert_equalIL`,
`assert_equalSL`, `assert_equalTL`, `assert_equal`/`assert_not_equal` (truthy
checks) — defined in `test/_assertions.mtl`. `assert_equalS` breaks on strings
containing `\0`; compare via `strget`/`strsub` instead for binary data.

## Manual/integration checks outside the automated suite

- `mtl/apps/sse/` — a standalone SSE broadcaster app (`task mtl:app-sse:simulate`),
  exercised with `curl -sN --noproxy localhost localhost:8080/`.
- `mtl/apps/template/` — the end-to-end proof that lib blocks compose into a
  runnable app; see its `README.md`.
