# lua/lib/hw — the Lua hardware behaviour layer (#263)

The behaviour half of the peripherals: the C HAL pokes registers, this decides
what the rabbit *does* with them. Per design principle 1 nothing here touches
hardware directly — every module is pure Lua over the thin `nab.*` bindings,
compiled off-device and shipped as `#LC` frames over the REPL, so **nothing in
this folder costs flash** (freezing a boot subset is #219's call).

Mirrors `mtl/lib/hw/`. The C side says so itself: `lua/firmware/inc/hal/motor.h`
documents that its position counter is "a raw, monotonically-wrapping 16-bit
edge count, not a homed/absolute position" and points at exactly the state
machine that lives here.

## Modules

| Module | Provides |
|--------|----------|
| `ears.lua` | ear homing + absolute positioning: hole counting, gap-landmark homing, shortest-path `move_to`, hand-turn detection, jam give-up |

## `ears.lua`

The wheel has 17 holes with **one double-width gap**, and that gap is the only
landmark on it. `nab.ear_pos(n)` counts edges and nothing else — it never says
which way the ear turned or where it points.

- **`home()`** spins the ear and times the interval between holes. An interval
  ≥1.5× the shortest one seen so far *is* the gap, which puts the ear on
  hardware zero — `OFFZERO` (2) holes short of ears-up — and the last two holes
  are counted out to position 0. The baseline is the running minimum, not the
  previous interval: that ignores motor spin-up (early intervals are long) and
  upward jitter, while the thing being looked for is 2×.
- **`move_to(n, p)`** counts holes to `p` (0..16), shortest way round unless a
  direction is forced. Arrival is `remaining <= 0`, not `== target`, so a poll
  that catches several edges at once still stops.
- **Touch**: counter movement while idle (past a short post-stop coast window)
  is a hand turn. The encoder cannot say which way, so the position is *voided*,
  not adjusted, and `on_touched(n, holes)` fires — mtl re-homed here too
  (`EARS_MODE_DETECT`). The app decides when to `home()` again.
- **Give-up**: no edge for `STALL` (3 s) while driving, or `MAXRUN` (10 s) on
  one move, stops the motor and flags the ear broken. A wedged motor is never
  left driving; `home()` clears the flag.

Naming: `move_to`, not `goto` — `goto` is a Lua 5.4 keyword and cannot be a
field name.

### Two things it needs from the caller

- **A running 1 ms tick.** Every timeout is `nab.time()`-based.
- **A prompt pump.** Edges are *counted*, not sampled, so a slow pump never
  loses position — but homing measures intervals, and jitter comparable to a
  hole (~60 ms at full duty) can look like the gap. Under ~10 ms per `:step()`
  the 1.5× margin holds comfortably; `:wait()` polls as fast as it can.

### API

```lua
local e = hw.ears.new(hw.ears.nabdrv())
e.on_touched = function(n, holes) ... end   -- optional

e:home([n])                 -- find the gap, park on position 0
e:move_to(n, p [, dir])     -- -> true | nil, "not homed"|"broken"|"bad ear"
e:stop([n])                 -- cut the motor, keep the position
e:step()                    -- pump from the app loop; true once idle
e:wait([ms])                -- blocking pump, for the REPL
e:position(n)               -- 0..16, or nil when not homed
e:target(n) · e:moving([n]) · e:idle() · e:homing(n) · e:broken(n)
```

`new(drv)` takes the whole HAL as a table (`time/pos/move/stop/sleep`), the
same injection `net.iface` uses — that is what makes the state machine
testable off-device against a fake wheel.

No speed argument: `nab.ear_move` is full-duty by design (#179 measured the
torque floor — below ~120/255 the gearmotors hum without turning, above it the
rate barely changes), so #263's optional "approach slowly" C change was
dropped. Arrival is decided by hole count, not by braking distance.

## Loading it, and why not line by line

`ears.lua` has multi-line function bodies, so it cannot go down the REPL's
per-line path: `replpipe.py`/`luash.py` compile **one frame per source line**
for a `.lua`, and only a `.lc` ships as a single chunk. Compile it whole, and
concatenate the driver into the same chunk (one `SCRIPT` per run):

```sh
cat lua/lib/hw/ears.lua lua/lib/hw/mydriver.lua > lua/.ears.lua
task lua:apps:compile APP=.ears.lua OUT=.ears.lc
task lua:firmware:flash:repl SCRIPT=.ears.lc     # paths are lua/-layer-relative
```

The driver, and what the #263 hardware DoD checks:

```lua
e = hw.ears.new(hw.ears.nabdrv())
e:home(); e:wait(); print(e:position(1), e:position(2))   --> 0  0
e:move_to(1, 8); e:wait(); print(e:position(1))           --> 8   (and it stays)
e.on_touched = function(n, d) print("ear", n, "turned", d) end
while true do e:step() end                   -- now turn an ear by hand
```

`task lua:firmware:flash:repl` drives it (see the `hw-flash-repl` skill).

## In the simulator

It loads and runs — the module was checked end to end as device bytecode on the
emulated ARM7 (`task lua:apps:simulate APP=<module+driver>.lc`): the bindings
resolve, `nab.time()` advances, and a `home()` that cannot finish stops the
motor and flags the ear broken rather than hanging.

It cannot *home* there. The simulator's synthetic ear encoder (#42) advances at
a uniform 8 counts/ms with no double-width gap, so the landmark homing looks
for does not exist — and it decrements on reverse, where the real FTM capture
counter only ever counts up (`hal/motor.h`, and `mtl/lib/hw/ears.mtl` applies
the direction in software for exactly that reason). Giving the sim's wheel real
hole geometry would make this simulable end to end; that is a change to #42's
model, not to this lib.

## Tests

`task lua:lib:test` runs `test/run.lua` under the `tools/luac` host `lua`
(same vendored tree + `LUA_32BITS` `luaconf.h` as the device, so the 16-bit
encoder and 32-bit tick wrap exactly as they do on the rabbit).

`test_ears.lua` drives a fake wheel — 17 holes, one double gap, motor spin-up,
±8 ms per-hole jitter — and asserts against **the wheel's own hole**, not just
the module's belief: after every move `hole == (position + OFFZERO) % 17`.
Covered: homing from all 17 start holes, homing across an encoder *and* tick
rollover, shortest-path and forced direction, ten hops with no drift, the coast
window vs. a real hand turn, jam and gapless-wheel give-up, and recovery.

`task lua:lib:size` (as of #263): `hw/ears` 3424 B stripped.
