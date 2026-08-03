-- hw.ears - ear homing + absolute positioning (#263): the hole-counting state
-- machine `hal/motor.h` deliberately left in the behaviour layer, ported from
-- mtl/lib/hw/ears.mtl to pure Lua over nab.ear_move/ear_stop/ear_pos.
--
-- The encoder is a free-running, wrapping 16-bit edge count - it tells you the
-- ear turned, never where it points. No mechanical end stop either: the ear
-- is a wheel, not an arm with a limit - an unstopped nab.ear_move() just keeps
-- going around, which is why home() has to find a landmark instead of driving
-- to a stop and zeroing there. The wheel has EARS_HOLES holes with one
-- double-width gap, and that gap is the only landmark on it: home() spins the
-- ear until it measures an inter-hole interval about 1.5x the shortest one
-- seen so far, which puts the ear at hardware zero, then counts OFFZERO more
-- holes to position 0 (ears up). From there every move is hole counting.
--
-- Speed: none. nab.ear_move is full-duty by design (#179 measured the torque
-- floor - below ~120/255 the gearmotors hum without turning, above it the rate
-- barely moves), so the "approach slowly" option #263 floated buys nothing;
-- arrival detection is by hole count, not by braking distance.
--
-- Pull-style like net/: the caller owns the clock and pumps :step() from the
-- cooperative loop. Everything is non-blocking except the :wait() convenience.
-- Pump it promptly - a poll gap longer than a hole (~60 ms at full speed) is
-- absorbed (edges are counted, not sampled), but a gap of several holes blurs
-- the interval timing homing needs.
--
--   local e = hw.ears.new(hw.ears.nabdrv())
--   e.on_touched = function(n, d) print("ear", n, "turned", d) end
--   e:home(); e:wait()          -- blocking, REPL
--   e:move_to(1, 8); e:wait()
--   print(e:position(1))        --> 8
--
-- Needs a running 1 ms tick, which rules out nothing: the simulator drives
-- counter_timer too. What the simulator cannot do is *home* - its synthetic
-- encoder (#42) runs at a uniform rate with no double gap, so there is no
-- landmark to find; the module correctly runs out its MAXRUN and reports the
-- ear broken there. Homing is proven on hardware.

hw = hw or {}
local ears = {}
hw.ears = ears

ears.HOLES = 17    -- holes per revolution; positions are 0..16
ears.OFFZERO = 2   -- holes from the hardware gap to position 0 (ears up)

-- tunables, all ms except TOUCH/GAP_*
ears.STALL = 3000    -- driving with no edge for this long -> jammed
ears.MAXRUN = 10000  -- hard cap on one move, jam or not (~9x a full rev)
ears.SETTLE = 300    -- post-stop coast window: edges here are not a hand turn
ears.TOUCH = 2       -- idle edges before we call it a hand turn (mtl: delta>2)
ears.GAP_NUM = 3     -- gap test: interval * GAP_DEN >= shortest * GAP_NUM,
ears.GAP_DEN = 2     -- i.e. 1.5x - the gap is 2x, normal jitter is well under

local HOLES, OFFZERO = ears.HOLES, ears.OFFZERO

-- nab.time() is a wrapping 32-bit tick (#259) and nab.ear_pos a wrapping
-- 16-bit count: both differences are taken modulo their width, never as plain
-- subtraction. On the device (LUA_32BITS) the 32-bit mask is a no-op; it is
-- what keeps the same code correct if the host lua ever has 64-bit integers.
local function elapsed(now, t0)
  return (now - t0) & 0xFFFFFFFF
end

local function edges(v, last)
  return (v - last) & 0xFFFF
end

-- the real hardware, after init_ears() (i.e. always, on the device)
function ears.nabdrv()
  return {time = nab.time, pos = nab.ear_pos, move = nab.ear_move,
          stop = nab.ear_stop, sleep = nab.wait}
end

-- drv = {time=fn()->ms, pos=fn(n)->raw16, move=fn(n,"forward"|"reverse"),
--        stop=fn(n), sleep=fn(ms)}  -- sleep only used by :wait()
function ears.new(drv)
  local self = {drv = drv, ear = {}, on_touched = nil}
  local t = drv.time()
  for n = 1, 2 do
    self.ear[n] = {n = n, raw = drv.pos(n), pos = nil, dir = nil,
                   phase = "idle", tedge = t, tstart = t, settle = t}
  end

  -- stop driving and open the coast window; keeps pos (the caller decides)
  local function halt(ear)
    drv.stop(ear.n)
    local now = drv.time()
    ear.phase, ear.dir, ear.target, ear.remain = "idle", nil, nil, nil
    ear.settle, ear.raw = now, drv.pos(ear.n)
  end

  -- give up on a jammed/unreadable ear: motor off first, position void
  local function giveup(ear)
    ear.broken, ear.pos = true, nil
    halt(ear)
  end

  local function start(ear, dir, phase, remain, target)
    local now = drv.time()
    ear.dir, ear.phase, ear.remain, ear.target = dir, phase, remain, target
    ear.tedge, ear.tstart, ear.raw = now, now, drv.pos(ear.n)
    ear.imin, ear.nint = nil, 0
    drv.move(ear.n, dir)
  end

  -- one ear, one poll. `now` is shared across both ears within a :step().
  local function pump(ear, now)
    local v = drv.pos(ear.n)
    local d = edges(v, ear.raw)

    if ear.phase == "idle" then
      if d > 0 then
        if elapsed(now, ear.settle) < ears.SETTLE then
          ear.raw = v                  -- the motor coasting to a halt
        elseif d >= ears.TOUCH then
          -- Somebody turned the ear. The encoder cannot tell us which way, so
          -- the position is gone, not adjusted - mtl re-acquired here too
          -- (EARS_MODE_DETECT re-homes). The app decides when to :home().
          ear.raw, ear.pos = v, nil
          if self.on_touched then self.on_touched(ear.n, d) end
        end
      end
      return
    end

    if d > 0 then
      -- Mean interval per hole: a slow pump can see several edges at once, and
      -- averaging keeps the gap test meaningful (a 2x gap shared with one
      -- normal hole still averages 1.5x). Clamp to 1 ms so a fast pump can
      -- never drive the baseline to zero and make everything look like a gap.
      local per = elapsed(now, ear.tedge) // d
      if per < 1 then per = 1 end
      ear.raw, ear.tedge = v, now

      if ear.phase == "home" then
        if ear.nint >= 2 and per * ears.GAP_DEN >= ear.imin * ears.GAP_NUM then
          -- We just crossed the double gap, so this hole is hardware zero -
          -- OFFZERO holes short of ears-up. Same direction, finish by count.
          ear.pos = (-OFFZERO) % HOLES
          ear.phase, ear.remain, ear.target = "goto", OFFZERO, 0
        else
          -- Baseline is the shortest interval seen, not the previous one: it
          -- ignores spin-up (early intervals are long) and upward jitter,
          -- while the thing we look for is 2x. Two samples before we arm, so
          -- starting inside the gap costs one extra revolution, not a miss.
          ear.nint = ear.nint + 1
          if ear.imin == nil or per < ear.imin then ear.imin = per end
        end
      else
        ear.pos = (ear.pos + (ear.dir == "forward" and d or -d)) % HOLES
        ear.remain = ear.remain - d
        if ear.remain <= 0 then      -- <=, not ==: a multi-edge poll overshoots
          ear.pos = ear.target
          halt(ear)
          return
        end
      end
    elseif elapsed(now, ear.tedge) > ears.STALL then
      return giveup(ear)             -- driving, encoder silent: jammed or dead
    end

    if elapsed(now, ear.tstart) > ears.MAXRUN then
      giveup(ear)                    -- turning but never arriving (no gap?)
    end
  end

  -- Pump both ears. Returns true once nothing is driving. Call it often.
  function self:step()
    local now = drv.time()
    pump(self.ear[1], now)
    pump(self.ear[2], now)
    return self:idle()
  end

  -- Find the gap and stop at position 0. Both ears unless n is given; clears
  -- the broken flag, so it is also the recovery path after a jam.
  function self:home(n)
    for i = 1, 2 do
      if n == nil or n == i then
        local ear = self.ear[i]
        ear.broken, ear.pos = false, nil
        start(ear, "forward", "home", nil, nil)
      end
    end
  end

  -- Point ear n at position p (0..HOLES-1, taken mod HOLES). Direction is the
  -- shortest way round unless dir ("forward"/"reverse") forces it.
  -- -> true | nil, reason
  function self:move_to(n, p, dir)
    local ear = self.ear[n]
    if ear == nil then return nil, "bad ear" end
    if ear.broken then return nil, "broken" end
    if ear.pos == nil then return nil, "not homed" end
    p = p % HOLES
    local fwd, rev = (p - ear.pos) % HOLES, (ear.pos - p) % HOLES
    if dir == nil then dir = fwd <= rev and "forward" or "reverse" end
    local remain = dir == "forward" and fwd or rev
    if remain == 0 then
      if ear.phase ~= "idle" then halt(ear) end
      return true
    end
    start(ear, dir, "goto", remain, p)
    return true
  end

  -- Cut the motor now (both ears unless n is given). Position is kept: we
  -- stopped it on purpose and the hole count is still good.
  function self:stop(n)
    for i = 1, 2 do
      if (n == nil or n == i) and self.ear[i].phase ~= "idle" then
        halt(self.ear[i])
      end
    end
  end

  function self:position(n) return self.ear[n].pos end   -- 0..16 | nil
  function self:target(n) return self.ear[n].target end
  function self:broken(n) return self.ear[n].broken == true end
  function self:homing(n) return self.ear[n].phase == "home" end

  function self:moving(n)
    if n then return self.ear[n].phase ~= "idle" end
    return self.ear[1].phase ~= "idle" or self.ear[2].phase ~= "idle"
  end

  function self:idle() return not self:moving() end

  -- Blocking convenience for the REPL: pump until every ear is idle. Returns
  -- true if they stopped, false on timeout (default: one MAXRUN plus slack, so
  -- the state machine's own give-up always fires first). Apps call :step().
  function self:wait(ms)
    local t0 = drv.time()
    ms = ms or ears.MAXRUN + 1000
    while not self:step() do
      if elapsed(drv.time(), t0) > ms then return false end
      if drv.sleep then drv.sleep(1) end
    end
    return true
  end

  -- Hand :step() to the cooperative reactor (#283), so the ears keep being
  -- pumped from every nab.wait/nab.delay and from the REPL's idle loop - not
  -- just from a loop the app remembers to write. Without this, any blocking
  -- call during a move means nothing checks the encoder and the ear sails past
  -- its target (the motor runs free once nab.ear_move starts it).
  --
  -- Guarded because `sched` is a device global: the host unit tests drive a
  -- fake drv with no reactor in sight, and :step() from a loop stays perfectly
  -- valid there. Returns self so it chains off new().
  function self:attach()
    if sched and not self.pumped then
      self.pumped = sched.pump(function() self:step() end)
    end
    return self
  end

  -- Give the slice back. Idempotent, and safe to call on a never-attached
  -- object: without it every ears/player ever created stayed in the reactor for
  -- the session, pinning itself and everything it references (#297).
  function self:detach()
    if self.pumped then
      sched.unpump(self.pumped)
      self.pumped = nil
    end
    return self
  end

  return self
end
