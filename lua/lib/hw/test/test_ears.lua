-- hw.ears: the hole-counting state machine against a fake ear.
--
-- The fake models the physical wheel, not the module's own idea of it: 17
-- holes with one double-width gap between hardware hole 16 and hole 0, a
-- spin-up delay before the first edge, and per-hole timing jitter. Every
-- assertion checks a position the module claims *and* where the wheel actually
-- ended up (hardware hole == (pos + OFFZERO) % HOLES), so a state machine that
-- merely agrees with itself fails.

local ears = hw.ears
local HOLES, OFFZERO = ears.HOLES, ears.OFFZERO

local SPACING = 60   -- ms per hole at full duty (~11 counts/700 ms, #179)
local SPINUP = 40    -- extra ms before the first edge after a start
local JITTER = 8     -- +-ms of per-hole timing noise

-- the fake rabbit ---------------------------------------------------------

-- o = {h1=,h2= start holes, raw= start encoder value, t0= start tick,
--      jam= ear that never turns, nogap= wheel with no double gap,
--      jitter= false to make the timing exact}
local function rig(o)
  o = o or {}
  local r = {n = 0, t0 = o.t0 or 0, seed = 12345, moves = 0, stops = 0,
             jam = o.jam, ear = {}}
  for i = 1, 2 do
    r.ear[i] = {h = (i == 1 and o.h1 or o.h2) or 0, raw = o.raw or 0,
                dir = nil, due = nil}
  end

  local function noise()
    if o.jitter == false then return 0 end
    r.seed = (r.seed * 1103515245 + 12345) & 0x7FFFFFFF
    return r.seed % (2 * JITTER + 1) - JITTER
  end

  -- ms from hole h to the next one in dir: the gap sits between 16 and 0
  local function space(h, dir)
    local wide = (dir == "forward" and h == HOLES - 1)
                 or (dir == "reverse" and h == 0)
    if o.nogap then wide = false end
    return (wide and 2 * SPACING or SPACING) + noise()
  end

  r.drv = {
    -- the tick wraps at 32 bits, exactly like counter_timer (#259)
    time = function() return r.t0 + r.n end,
    pos = function(i) return r.ear[i].raw end,
    move = function(i, dir)
      local e = r.ear[i]
      r.moves = r.moves + 1
      e.dir, e.due = dir, r.n + SPINUP + space(e.h, dir)
    end,
    stop = function(i)
      r.stops = r.stops + 1
      r.ear[i].dir, r.ear[i].due = nil, nil
    end,
    sleep = function(ms) r:tick(ms) end,
  }

  function r:tick(ms)
    for _ = 1, ms do
      self.n = self.n + 1
      for i = 1, 2 do
        local e = self.ear[i]
        if e.dir and i ~= self.jam and self.n >= e.due then
          e.h = (e.h + (e.dir == "forward" and 1 or -1)) % HOLES
          e.raw = (e.raw + 1) & 0xFFFF
          e.due = self.n + space(e.h, e.dir)
        end
      end
    end
  end

  -- a hand turn: k holes forward, encoder counting as always
  function r:turn(i, k)
    local e = self.ear[i]
    e.h, e.raw = (e.h + k) % HOLES, (e.raw + k) & 0xFFFF
  end

  return r
end

-- pump until everything is idle; `poll` ms of fake time per :step()
local function run(e, r, poll, limit)
  poll, limit = poll or 1, limit or 40000
  local spent = 0
  while not e:step() do
    r:tick(poll)
    spent = spent + poll
    if spent > limit then return false end
  end
  return true
end

-- homing ------------------------------------------------------------------

-- From every one of the 17 start holes, with jitter on: the gap is found and
-- the ear parks on position 0, i.e. hardware hole OFFZERO.
local homed_all, parked_all = true, true
for h = 0, HOLES - 1 do
  local r = rig{h1 = h}
  local e = ears.new(r.drv)
  e:home(1)
  if not run(e, r, 1) then homed_all = false end
  if e:position(1) ~= 0 or r.ear[1].h ~= OFFZERO or e:broken(1) then
    parked_all = false
    print(("  home from hole %d: pos %s, wheel at %d")
          :format(h, tostring(e:position(1)), r.ear[1].h))
  end
end
ok(homed_all, "home terminates from all 17 start holes")
ok(parked_all, "home parks at position 0 / wheel hole 2 from all start holes")

-- Same, with the encoder about to wrap its 16 bits and the tick about to wrap
-- its 32: the deltas are modular, so neither rollover disturbs anything.
local r = rig{h1 = 1, raw = 0xFFF0, t0 = 0x7FFFFFFF - 500}
local e = ears.new(r.drv)
e:home(1)
ok(run(e, r, 1), "home completes across encoder + tick rollover")
eq(e:position(1), 0, "rollover: homed to position 0")
eq(r.ear[1].h, OFFZERO, "rollover: wheel parked on hole 2")
ok(r.ear[1].raw < 0xFFF0, "rollover: encoder wrapped past 0xFFFF")
ok(r.drv.time() < 0, "rollover: tick wrapped into negative 32-bit space")

-- Mid-flight: finding the gap puts the ear on hardware zero, which is OFFZERO
-- holes short of ears-up - so the position reads HOLES-OFFZERO for the last
-- stretch, and only then 0. This pins the offset the parked-hole assertions
-- above cannot see (they are the same wheel hole either way).
r = rig{h1 = 5}
e = ears.new(r.drv)
e:home(1)
local at_gap = nil
while not e:step() do
  if at_gap == nil and not e:homing(1) then at_gap = e:position(1) end
  r:tick(1)
end
eq(at_gap, HOLES - OFFZERO, "the gap hole reads as position 15, two short of 0")
eq(e:position(1), 0, "and the two-hole approach finishes on 0")

-- A slower pump still homes: 5 ms polls against a ~60 ms hole leave the 1.5x
-- gap margin intact (a pump as coarse as the hole itself would not - see the
-- module header).
r = rig{h1 = 3}
e = ears.new(r.drv)
e:home(1)
ok(run(e, r, 5), "home completes with a 5 ms pump")
eq(e:position(1), 0, "5 ms pump: homed to position 0")
eq(r.ear[1].h, OFFZERO, "5 ms pump: wheel parked on hole 2")

-- Both ears home at once, from different holes.
r = rig{h1 = 4, h2 = 12}
e = ears.new(r.drv)
e:home()
ok(run(e, r, 1), "both ears home concurrently")
eq(e:position(1), 0, "ear 1 homed")
eq(e:position(2), 0, "ear 2 homed")
eq(r.ear[1].h, OFFZERO, "ear 1 wheel parked")
eq(r.ear[2].h, OFFZERO, "ear 2 wheel parked")

-- a homed rig for the positioning tests
local function homed(o)
  local rr = rig(o)
  local ee = ears.new(rr.drv)
  ee:home(1)
  assert(run(ee, rr, 1), "fixture failed to home")
  return ee, rr
end

-- goto: direction, arrival, no drift ---------------------------------------

e, r = homed()
ok(e:move_to(1, 8), "move_to 8 accepted")
eq(r.ear[1].dir, "forward", "0 -> 8 takes the short way forward")
ok(run(e, r, 1), "move_to 8 completes")
eq(e:position(1), 8, "position 8 reported")
eq(r.ear[1].h, (8 + OFFZERO) % HOLES, "wheel actually on hole 10")

-- 15 is 15 holes forward but only 2 back: shortest wins.
e, r = homed()
e:move_to(1, 15)
eq(r.ear[1].dir, "reverse", "0 -> 15 takes the short way back")
ok(run(e, r, 1), "move_to 15 completes")
eq(e:position(1), 15, "position 15 reported")
eq(r.ear[1].h, (15 + OFFZERO) % HOLES, "wheel actually on hole 0")

-- an explicit direction overrides the shortest path
e, r = homed()
e:move_to(1, 15, "forward")
eq(r.ear[1].dir, "forward", "explicit forward honoured")
ok(run(e, r, 1), "forced-direction move completes")
eq(e:position(1), 15, "forced direction still lands on 15")

-- ten hops in a row, checked against the wheel every time: hole counting must
-- not accumulate error.
e, r = homed()
local drift = nil
for _, p in ipairs{5, 16, 1, 9, 9, 0, 13, 4, 12, 2} do
  e:move_to(1, p)
  if not run(e, r, 1) then drift = "timeout at " .. p end
  if not drift and e:position(1) ~= p then drift = "believed " .. p end
  if not drift and r.ear[1].h ~= (p + OFFZERO) % HOLES then
    drift = ("wheel on %d, wanted %d"):format(r.ear[1].h, (p + OFFZERO) % HOLES)
  end
end
eq(drift, nil, "10 consecutive moves land exactly, no drift")

-- already there: no motor start at all
e, r = homed()
local moves = r.moves
ok(e:move_to(1, 0), "move_to current position accepted")
eq(r.moves, moves, "move_to current position never starts the motor")
ok(e:idle(), "move_to current position leaves the ear idle")

-- A pump coarser than a hole sees several edges at once. The count still
-- terminates on the target (the wheel physically overshoots - that is the
-- pump's fault, not the state machine's).
e, r = homed()
e:move_to(1, 6)
ok(run(e, r, 200), "multi-edge polls still terminate")
eq(e:position(1), 6, "multi-edge polls stop on the target count")

-- refusals ----------------------------------------------------------------

r = rig{}
e = ears.new(r.drv)
local okv, why = e:move_to(1, 5)
eq(okv, nil, "move_to before home refused")
eq(why, "not homed", "...with reason 'not homed'")
okv, why = e:move_to(3, 5)
eq(okv, nil, "move_to on a bad ear refused")
eq(why, "bad ear", "...with reason 'bad ear'")

-- touch --------------------------------------------------------------------

e, r = homed()
local touch = {}
e.on_touched = function(n, d) touch[#touch + 1] = {n = n, d = d} end

-- straight after a stop the motor is still coasting: not a hand turn
r:turn(1, 3)
e:step()
eq(#touch, 0, "coast right after a stop is not reported as a touch")
eq(e:position(1), 0, "coast does not void the position")

-- past the settle window it is
r:tick(ears.SETTLE + 1)
r:turn(1, 4)
e:step()
eq(#touch, 1, "hand turn after the settle window fires on_touched")
eq(touch[1].n, 1, "on_touched reports ear 1")
eq(touch[1].d, 4, "on_touched reports 4 holes of movement")
eq(e:position(1), nil, "a hand turn voids the position (direction unknown)")
okv, why = e:move_to(1, 5)
eq(why, "not homed", "move_to after a touch asks for a re-home")

-- a nudge below the threshold is noise, not a turn
e, r = homed()
touch = {}
e.on_touched = function(n, d) touch[#touch + 1] = {n = n, d = d} end
r:tick(ears.SETTLE + 1)
r:turn(1, 1)
e:step()
eq(#touch, 0, "a single stray edge is not a touch")
eq(e:position(1), 0, "a single stray edge keeps the position")

-- jam / give-up ------------------------------------------------------------

-- motor driven, encoder silent: stop within STALL and stay stopped
r = rig{jam = 1}
e = ears.new(r.drv)
e:home(1)
ok(run(e, r, 1, ears.STALL + 500), "a jammed ear gives up, it does not hang")
ok(e:broken(1), "a jammed ear is flagged broken")
eq(r.ear[1].dir, nil, "a jammed motor is never left driving")
eq(e:position(1), nil, "a jammed ear has no position")
okv, why = e:move_to(1, 5)
eq(why, "broken", "move_to on a broken ear is refused")

-- turning but never arriving (a wheel with no gap): the hard cap fires
r = rig{nogap = true}
e = ears.new(r.drv)
e:home(1)
ok(run(e, r, 1, ears.MAXRUN + 500), "a gapless wheel gives up at MAXRUN")
ok(e:broken(1), "a gapless wheel is flagged broken")
eq(r.ear[1].dir, nil, "the gapless wheel's motor is stopped")

-- home() is the recovery path: it clears broken and works again
r = rig{h1 = 7, jam = 1}
e = ears.new(r.drv)
e:home(1)
run(e, r, 1, ears.STALL + 500)
ok(e:broken(1), "ear broken before recovery")
r.jam = nil
e:home(1)
ok(run(e, r, 1), "home() recovers a previously jammed ear")
eq(e:broken(1), false, "home() clears the broken flag")
eq(e:position(1), 0, "the recovered ear homes to position 0")

-- stop / wait --------------------------------------------------------------

-- an explicit stop cuts the motor and keeps what we counted so far (exact
-- timing here: the assertion is about *which* hole we stopped on)
e, r = homed{jitter = false}
e:move_to(1, 8, "forward")
r:tick(SPINUP + 3 * SPACING)
e:step()
e:stop()
ok(e:idle(), "stop() leaves the ear idle")
eq(r.ear[1].dir, nil, "stop() cuts the motor")
eq(e:position(1), 3, "stop() keeps the holes counted so far")

-- the blocking REPL wrapper drives the machine through drv.sleep
r = rig{h1 = 11}
e = ears.new(r.drv)
e:home(1)
ok(e:wait(), "wait() returns true once homing finishes")
eq(e:position(1), 0, "wait() homed to position 0")
e:move_to(1, 4)
ok(e:wait(), "wait() returns true once the move finishes")
eq(e:position(1), 4, "wait() moved to position 4")
eq(r.ear[1].h, (4 + OFFZERO) % HOLES, "wait(): wheel actually on hole 6")

-- wait() gives up on its own deadline too
r = rig{jam = 1}
e = ears.new(r.drv)
e:home(1)
eq(e:wait(ears.STALL // 2), false, "wait() returns false on timeout")
