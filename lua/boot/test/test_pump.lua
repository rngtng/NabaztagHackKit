-- Unit tests for the COOPERATIVE PUMP's contract - utils/pump.c's four rules,
-- as modelled by run.lua's dispatch() (#329).
--
-- These are not tests of `sched` (that is test_sched.lua). They are tests of
-- the seam underneath it, and they exist because that seam had a trap in it
-- that no test could reach: the pump's busy guard means a nab.wait() called
-- from inside a callback, a sched pump or a spawned task delivers NOTHING for
-- its whole duration - the #283 hole, reopened one level down. It went
-- unnoticed until a review read the C by hand, and it stayed unnoticed here
-- because this harness modelled nab_pump() as a plain function call and
-- nab.wait() as a no-op. Both are modelled properly now, and every scenario
-- below fails against a harness that goes back to not modelling the guard.
--
-- The rules, as utils/pump.h states them:
--   1. a nested dispatch delivers nothing (or nab.wait inside a callback would
--      recurse until the stack gave out);
--   2. the pollers run anyway, even when re-entered - so an edge that happens
--      entirely inside a nested wait is still SEEN, and delivered afterwards;
--   3. drain the C queue, then hand the reactor its tick slice;
--   4. a raising callback prints and dispatch continues.

-- ---------------------------------------------------------------------------
-- Baseline: the modelled pump delivers at all. If these fail, everything
-- below passes or fails for the wrong reason.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local seen = {}
  nab.on("button", function(pressed) seen[#seen + 1] = "button:" .. tostring(pressed) end)
  sched.pump(function() seen[#seen + 1] = "tick" end)

  nab_inject(1000, "button", true)
  nab_inject(1000, "button", false)
  nab_pump()

  -- Rule 3, and the order it fixes: every queued event first, the reactor
  -- slice once, after the queue is empty.
  eq(table.concat(seen, ","), "button:true,button:false,tick",
     "pump: drains the event queue, THEN ticks")
end

do
  boot_reload()
  nab_set_time(1000)

  local ticks, delivered = 0, 0
  nab.on("button", function() delivered = delivered + 1 end)
  sched.pump(function() ticks = ticks + 1 end)

  -- An edge the pollers cannot see yet stays on the hardware, not in the queue.
  nab_inject(1010, "button", true)
  nab_pump()
  eq(delivered, 0, "pump: an edge in the future is not delivered early")
  eq(ticks, 1, "pump: the tick slice runs anyway")

  nab_advance(10)
  nab_pump()
  eq(delivered, 1, "pump: the edge is delivered once its moment has come")
  eq(ticks, 2, "pump: and the tick slice keeps running")
end

-- ---------------------------------------------------------------------------
-- Rule 4: a raising callback is contained; dispatch carries on.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local ticks, second = 0, 0
  nab.on("button", function() error("callback exploded") end)
  nab.on("rfid", function() second = second + 1 end)
  sched.pump(function() ticks = ticks + 1 end)

  nab_inject(1000, "button", true)
  nab_inject(1000, "rfid", "d0021a3506198b86")
  local err = nab_pump()

  ok(err and err:find("callback exploded", 1, true) ~= nil,
     "pump: a raising callback is reported, not propagated")
  eq(second, 1, "pump: the NEXT queued event is still delivered")
  eq(ticks, 1, "pump: and the reactor still gets its slice")
end

-- ---------------------------------------------------------------------------
-- The top-level contract: nab.wait DOES pump. This is the half the `nab`
-- reference has always described, and the half that makes
-- `while true do nab.wait(100) end` a working main loop.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local ticks, delivered = 0, 0
  nab.on("button", function() delivered = delivered + 1 end)
  sched.pump(function() ticks = ticks + 1 end)

  nab_inject(1005, "button", true)
  nab.wait(20)   -- from the top level: the REPL, or a script's main loop

  eq(nab.time(), 1020, "nab.wait: advances the 1 ms tick by ms")
  eq(delivered, 1, "nab.wait: at top level, an edge mid-wait IS delivered")
  eq(ticks, 21, "nab.wait: at top level, the reactor is ticked every ms")
end

-- ---------------------------------------------------------------------------
-- Rule 1, and the trap: nab.wait from INSIDE the reactor delivers nothing.
--
-- This is the assertion the whole file is for. Same 20 ms wait as above, moved
-- inside a sched pump: the guard turns it into 20 ms of dead air. Note that
-- the numbers are asserted, not the absence of a crash - "it did not recurse"
-- would also be true of a harness that never dispatched at all.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local runs, others, delivered = 0, 0, 0
  local polls_before, polls_after
  nab.on("button", function() delivered = delivered + 1 end)
  sched.pump(function()
    runs = runs + 1
    if runs == 1 then
      polls_before = nab_polls()
      nab.wait(20)          -- the trap: 20 ms in which the reactor is frozen
      polls_after = nab_polls()
    end
  end)
  sched.pump(function() others = others + 1 end)

  nab_inject(1005, "button", true)
  nab_inject(1010, "button", false)
  nab_pump()

  -- Rule 1: no recursive dispatch. The waiting pump ran once, the pump behind
  -- it ran once (after the wait returned), and nothing was delivered.
  eq(runs, 1, "nab.wait inside a pump: does NOT re-enter the reactor")
  eq(others, 1, "nab.wait inside a pump: the next pump runs once, after it")
  eq(delivered, 0, "nab.wait inside a pump: queued events are NOT delivered")
  eq(nab.time(), 1020, "nab.wait inside a pump: time passes all the same")

  -- Rule 2: the pollers ran throughout - once for the wait's opening dispatch
  -- and once per millisecond. That is what keeps the press+release below from
  -- being lost rather than merely late, and it is the one thing that
  -- distinguishes this guard from putting event_pump() inside it.
  eq(polls_after - polls_before, 21,
     "nab.wait inside a pump: the pollers keep sampling the hardware")

  -- ...and the proof: both edges of a press+release that happened entirely
  -- inside the frozen window arrive on the next pump, in order.
  local order = {}
  nab.on("button", function(pressed) order[#order + 1] = tostring(pressed) end)
  nab_pump()
  eq(table.concat(order, ","), "true,false",
     "nab.wait inside a pump: edges from the frozen window arrive after it")
end

-- ---------------------------------------------------------------------------
-- The same trap from a spawned task, which is where it is most tempting to
-- write. sched.sleep(ms) is the cooperative spelling; nab.wait(ms) blocks the
-- whole reactor, including every other task's deadline.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local steps = {}
  -- Spawned first, so it is already asleep on a 10 ms deadline when the
  -- blocker starts: the deadline then falls INSIDE the blocker's wait.
  sched.spawn(function()
    steps[#steps + 1] = "other:start"
    sched.sleep(10)
    steps[#steps + 1] = "other:resumed"
  end)
  sched.spawn(function()
    steps[#steps + 1] = "blocker:start"
    nab.wait(50)                       -- NOT sched.sleep: this freezes everyone
    steps[#steps + 1] = "blocker:done"
  end)

  nab_pump()
  -- Both tasks got their first slice, and the blocker ran to completion inside
  -- this one pump (nab.wait is a plain call, not a yield). The other task's
  -- 10 ms deadline came and went 40 ms before that, unserviced.
  eq(table.concat(steps, ","), "other:start,blocker:start,blocker:done",
     "nab.wait inside a task: no other task is resumed during the wait")
  eq(nab.time(), 1050, "nab.wait inside a task: 50 ms went by")

  nab_pump()
  eq(table.concat(steps, ","),
     "other:start,blocker:start,blocker:done,other:resumed",
     "nab.wait inside a task: the other task resumes on the next pump - 40 ms late")
end

-- ---------------------------------------------------------------------------
-- The guard is not a one-shot: it releases when dispatch finishes, so the pump
-- after a blocked one behaves normally again. Without this, "delivers nothing"
-- could be satisfied by a harness that simply stopped delivering.
-- ---------------------------------------------------------------------------

do
  boot_reload()
  nab_set_time(1000)

  local delivered = 0
  nab.on("button", function() delivered = delivered + 1 end)
  local waited = false
  sched.pump(function()
    if not waited then
      waited = true
      nab.wait(5)
    end
  end)

  -- Due 3 ms into the wait, so it is queued by the pollers while the guard is
  -- held rather than before dispatch ever started.
  nab_inject(1003, "button", true)
  nab_pump()
  eq(nab.time(), 1005, "guard: the waiting callback took its 5 ms")
  eq(delivered, 0, "guard: nothing is delivered while a callback waits")

  nab_inject(1010, "button", false)
  nab_advance(5)
  nab_pump()
  eq(delivered, 2, "guard: releases - the next pump delivers both edges")
end
