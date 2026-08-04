-- Unit tests for `sched`, the cooperative reactor in boot.lua (#283).
--
-- Driven through the modelled device seam in run.lua: the test owns the clock
-- and calls nab_pump() where main.c's dispatch_events() would, so every
-- assertion is about sched itself and nothing depends on wall time.
--
-- Per the repo testing rule every check names the value it expects. The
-- baseline scenarios below (scheduling, wrap-around, task-error isolation)
-- exist so the suite cannot pass vacuously: if sched stopped running anything
-- at all, they would fail too.

-- ---------------------------------------------------------------------------
-- Baseline: the reactor does what it says on the tin.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local a, b = 0, 0
  sched.pump(function() a = a + 1 end)
  sched.pump(function() b = b + 1 end)

  nab_pump()
  eq(a, 1, "sched: first pump runs once per tick")
  eq(b, 1, "sched: second pump runs once per tick")
  nab_pump()
  eq(a, 2, "sched: first pump runs again on the next tick")
  eq(b, 2, "sched: second pump runs again on the next tick")
end

do
  boot_reload()
  nab_set_time(1000)

  local steps = {}
  sched.spawn(function()
    steps[#steps + 1] = "a"
    sched.sleep(50)
    steps[#steps + 1] = "b"
  end)

  nab_pump()
  eq(table.concat(steps, ","), "a", "sched.spawn: runs the task on the next tick")
  nab_advance(10)
  nab_pump()
  eq(table.concat(steps, ","), "a", "sched.sleep: task stays asleep before its deadline")
  nab_advance(45)
  nab_pump()
  eq(table.concat(steps, ","), "a,b", "sched.sleep: task resumes once the deadline passes")
  nab_pump()
  eq(table.concat(steps, ","), "a,b", "sched: a finished task is dropped, not re-resumed")
end

do
  -- Deadlines are compared as a signed difference so they survive the 32-bit
  -- tick wrapping (LUA_32BITS makes nab.time() a wrapping 32-bit integer).
  boot_reload()
  nab_set_time(0x7FFFFFF0)

  local woke = false
  sched.spawn(function() sched.sleep(0x20); woke = true end)
  nab_pump()
  eq(woke, false, "sched: task asleep across the tick wrap is not resumed early")
  nab_set_time(0x7FFFFFF0 + 0x20)   -- wraps to a negative Lua integer
  nab_pump()
  eq(woke, true, "sched: task wakes on its deadline across the 32-bit tick wrap")
end

do
  -- A task that raises is reported and dropped; the other tasks keep running.
  -- This half of principle 3 is already honoured - it is the pump half below
  -- that is not, and this baseline is what makes that asymmetry visible.
  boot_reload()
  nab_set_time(1000)

  local survivor = 0
  sched.spawn(function() error("task blew up") end)
  sched.spawn(function() while true do survivor = survivor + 1; sched.sleep(0) end end)

  eq(nab_pump(), nil, "sched: a raising TASK does not propagate out of the tick")
  ok(survivor >= 1, "sched: the other task still ran after a task raised")
  local before = survivor
  nab_pump()
  ok(survivor > before, "sched: the surviving task keeps running on later ticks")
end

-- ---------------------------------------------------------------------------
-- DEFECT 1 - a pump callback that raises takes the whole reactor down, for
-- good.
--
-- sched.tick() calls `pumps[i]()` bare, while the task half wraps every
-- resume in coroutine.resume and drops the offender. So one raising pump:
--
--   * aborts sched.tick() at that index - every LATER pump and EVERY task is
--     skipped for that slice;
--   * is never removed, so the same abort repeats on every single pump
--     iteration, forever. Nothing recovers: sched exposes no way to remove a
--     pump, and the app's own code is downstream of the abort;
--   * escapes into C, where dispatch_events()'s lua_pcall + report() prints it
--     to the console on every iteration of the REPL idle loop.
--
-- boot.lua's own header states the contract this breaks: "an error is printed
-- and the rest keep running (principle 3 - one broken activity must not take
-- the runtime down)". It is true of tasks and false of pumps.
--
-- This is not a hypothetical shape. :attach() is the documented way to join
-- the reactor and both users of it hand over a closure that can raise -
-- hw.ears's `self:step()` indexes self.ear[n] and calls drv.pos(), and
-- audio.player's `self:step()` calls src:pull() on a source object the app
-- supplied. A stream source whose connection object goes away raises inside
-- the pump, and from that moment the ears stop being stepped, every spawned
-- choreography freezes and the net is never polled again - while the console
-- fills with one copy of the message per pump.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local later_pump, task_slices = 0, 0
  sched.pump(function() error("a pump blew up") end)
  sched.pump(function() later_pump = later_pump + 1 end)
  sched.spawn(function() while true do task_slices = task_slices + 1; sched.sleep(0) end end)

  eq(nab_pump(), nil, "sched: a raising PUMP does not propagate out of the tick")
  eq(later_pump, 1, "sched: the pump after a raising one still ran")
  eq(task_slices, 1, "sched: spawned tasks still ran after a pump raised")

  -- ...and it must not be a one-slice hiccup either: the reactor has to keep
  -- working on every following tick, not wedge on the same broken pump.
  for _ = 1, 3 do nab_advance(10); nab_pump() end
  eq(later_pump, 4, "sched: the good pump keeps running on later ticks")
  eq(task_slices, 4, "sched: spawned tasks keep running on later ticks")
end

-- ---------------------------------------------------------------------------
-- DEFECT 2 - a pump can be registered but never removed.
--
-- sched.pump() appends to a list with no handle and no counterpart. Every
-- consequence follows from that one gap:
--
--   * :attach() is not idempotent. `p:attach()` twice - or once per REPL line
--     while iterating on a script - registers the same :step() twice and it is
--     called twice per slice for the rest of the session.
--   * a finished object is pumped forever. audio.player's :step() returns
--     "idle" and hw.ears's returns immediately, so it is "only" wasted work -
--     but it is wasted work per pump iteration, i.e. inside every nab.wait(),
--     nab.play() and wifi_recv() the firmware runs, on a 32 MHz ARM7.
--   * the list grows without bound across a session. Each new
--     audio.player.new(...):attach() (one per clip, in the obvious usage) adds
--     a closure that pins the whole player - source, queue and its 2 KB TAIL
--     string - so nothing it references can ever be collected.
--   * it is the only possible recovery from DEFECT 1, and it does not exist.
--
-- The fix wants a handle: `local h = sched.pump(fn)` ... `sched.unpump(h)`,
-- with :attach()/:detach() on the two users. The assertions below are written
-- against that shape because it is the minimum that makes any of the four
-- points above fixable.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  eq(type(sched.unpump), "function", "sched.unpump must exist to undo sched.pump")

  local n = 0
  local h = sched.pump(function() n = n + 1 end)
  ok(h ~= nil, "sched.pump must return a handle its caller can unregister with")

  nab_pump()
  eq(n, 1, "sched: the registered pump ran once")

  if type(sched.unpump) == "function" and h ~= nil then
    sched.unpump(h)
    nab_pump()
    eq(n, 1, "sched.unpump: the removed pump is not called again")
    -- Removing an already-removed handle is a no-op, not an error: an object's
    -- :detach() must be safe to call from both a stop() path and a finaliser.
    eq(pcall(sched.unpump, h), true, "sched.unpump: removing twice is a no-op")
  end
end

-- ---------------------------------------------------------------------------
-- DEFECT 3 - the reactor hangs off a single-slot callback that boot.lua claims
-- at startup, and any app that uses the documented seam silently unhooks it.
--
-- main.c's nab_on() stores one function per event name (`lua_setfield(L, -2,
-- event_names[which])`). boot.lua ends with `nab.on("tick", sched.tick)`.
-- firmware/README.md documents "tick" as a public nab.on source - "fn() on
-- every pump iteration" - so an app doing exactly what the table says:
--
--     nab.on("tick", function() frame = frame + 1 end)
--
-- replaces sched.tick outright. From that line on, every :attach()ed ears and
-- player stops being stepped and every sched.spawn'd choreography freezes -
-- with no error, no warning and no way to tell from the app that the reactor
-- it depends on is gone. The failure surfaces later and somewhere else: an ear
-- sails past its target because nothing is counting holes, or a clip
-- underruns because nothing is feeding the decoder.
--
-- This is the composition hole #283 set out to close, one level up: #283 fixed
-- "a blocking call is a hole in which nothing else runs", and left "an app
-- registering a tick callback is a hole in which nothing else runs".
--
-- The test asserts the outcome, not a mechanism, so either fix passes:
-- chaining the seam in C (nab.on("tick") appends), or boot.lua owning the slot
-- and routing app callbacks through sched.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local pumped, attached = 0, 0
  sched.pump(function() pumped = pumped + 1 end)   -- e.g. an :attach()ed ears
  sched.spawn(function() while true do attached = attached + 1; sched.sleep(0) end end)

  nab_pump()
  eq(pumped, 1, "sched: the reactor runs before the app registers anything")

  -- The app does what firmware/README.md documents.
  local app_ticks = 0
  nab.on("tick", function() app_ticks = app_ticks + 1 end)

  nab_advance(10)
  nab_pump()
  eq(app_ticks, 1, "nab.on('tick'): the app's own callback runs")
  eq(pumped, 2, "nab.on('tick'): an app tick must not stop the reactor's pumps")
  eq(attached, 2, "nab.on('tick'): an app tick must not freeze spawned tasks")
end
