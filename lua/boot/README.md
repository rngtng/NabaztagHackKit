# lua/boot — the resident boot chunk

`boot.lua` is the one piece of Lua that ships **inside** the firmware image. The
image is parser-less (#128), so it cannot be compiled at startup: `task
lua:boot:compile` runs it through the off-device `luac` into `gen/boot_lc.h`,
and `src/main.c` loads that blob with `luaL_loadbuffer` before the REPL starts.
`task lua:firmware:build` delegates the embed here — edit `boot.lua`, never the
generated header.

It costs **flash** (3,674 B, measured off `gen/boot_lc.h` from a real
`task lua:firmware:build`; see
[`../firmware/README.md`](../firmware/README.md#flash-budget)), which is the
whole reason it is short.

## What is in it

Two very different things, and it is worth knowing which is which:

- **`sched` — the cooperative reactor (#283).** A core runtime service, not a
  demo. It is driven from `nab.on("tick")`, which the C pump calls after
  draining the event queue, so it runs from the REPL's idle loop *and* from
  inside every `nab.wait()`/`nab.delay()`/`nab.play()`/`nab.wifi_recv()`. Two
  entry points: `sched.pump(fn)` for the pull-style state machines that already
  exist (`hw.ears:step()`, `net.iface:poll()`, `audio.player:step()` — all
  reachable via their `:attach()`), and `sched.spawn`/`sched.sleep` for
  sequential behaviour written as a coroutine. It is resident rather than
  REPL-loaded because an app that has to load its own scheduler before it can
  keep an ear on target is not much of a fix.
- **Demo helpers** — `run`/`watch`/`ledshow`/`greenmode` and two hard-coded RFID
  UIDs. These largely duplicate [`../apps/`](../apps/) and are the cheaper half
  of the two remaining flash levers named in the firmware README (a product
  decision, not a refactor).

## Tests

```sh
task lua:boot:test        # host-lua unit tests for sched; part of lua:verify
task lua:boot:compile     # standalone, this doubles as a compile/lint check
```

`test/run.lua` models the device seam faithfully rather than conveniently: the
test owns `nab.time()`, and `nab.on` holds **one** callback per name and
replaces it silently, because that is exactly what `utils/pump.c`'s `pump_on`
does. `nab_pump()` is one iteration of `pump_dispatch()` — the whole of it, C
rule for C rule: the pollers run first and unconditionally, the re-entrancy
guard makes a nested dispatch deliver nothing, the queue is drained before the
reactor gets its tick slice, and a raising callback is contained by `pcall`.
`nab.wait(ms)` advances the modelled 1 ms tick dispatching at every step, as
`nab_wait` does. `boot.lua` itself is loaded verbatim from source, so there is
no copy to drift, and the same stdlib lint the lib tests run applies here —
with `coroutine` allowed, since the resident chunk is what uses it.

That claim was **not** true until #329: `nab_pump()` was a plain function call
that did not model the guard at all, and `nab.wait()` was a no-op — so the one
suite whose job is protecting the reactor could not tell a pump that keeps
running from one that has silently stopped, which is precisely how the
nested-`nab.wait` trap went unnoticed. `test/test_pump.lua` is the part that
holds the seam to its contract; every scenario in it fails against a harness
that drops the guard or moves the pollers inside it.

The suite carries baseline scenarios (scheduling, the 32-bit tick wrap,
task-error isolation, an event actually being delivered) alongside the
regression guards, so it cannot pass vacuously: if `sched` or the modelled
pump stopped running anything at all, the baselines would fail too.

Two test files, and the split is the seam itself: `test_sched.lua` is the
reactor (pumps, tasks, deadlines, error isolation), `test_pump.lua` is the C
pump underneath it (the four rules in
[`../firmware/inc/utils/pump.h`](../firmware/inc/utils/pump.h), and what
`nab.wait` does on either side of the guard).
