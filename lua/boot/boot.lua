-- Resident boot chunk for the Lua firmware (#128).
--
-- The bytecode-only image has no on-device parser, so this cannot be compiled
-- at startup - the build compiles it off-device (tools/luac/embed.py) into
-- gen/boot_lc.h and main.c loads that blob via luaL_loadbuffer. Keep it small:
-- it is baked into the 124 KB flash image.
--
-- It defines the M5 nab-binding demo helpers plus a short LED showcase (#102),
-- sets an idle LED state, then returns to the REPL. It does NOT auto-run
-- anything long: run() is a while-true RFID loop that only returns on a
-- head-button press, and ledshow() is a ~6 s animation - auto-calling either at
-- boot would delay or strand the REPL (the boot chunk eats the instruction
-- budget before the prompt, #207). Both bodies are resident, so trigger them
-- with a short line at the prompt: run() or ledshow(). ledshow() doubles as a
-- timer/fade self-test - smooth breathing means the 1 ms timer IRQ + background
-- fade engine work; a jump means the timer isn't firing. The fuller show is
-- ../apps/led-demo.lua.

-- ---- sched: the cooperative reactor (#283) ---------------------------------
-- One place that advances everything, so the four workloads compose. Two ways
-- in, matching the two shapes work already has here:
--
--   sched.pump(fn)   fn() every pump iteration - for the pull-style state
--                    machines that already exist (hw.ears :step(), net.iface
--                    :poll()). They were correct all along; nothing was calling
--                    them during a blocking stretch.
--   sched.spawn(fn)  run fn as a coroutine; it calls sched.sleep(ms) to yield
--                    until a deadline. For sequential behaviour (a choreography,
--                    a fetch-then-speak) that reads top-to-bottom instead of
--                    being hand-unrolled into a state machine.
--
-- It is driven from nab.on("tick"), which the C pump calls after draining the
-- event queue - so it runs from the REPL's idle loop AND from inside every
-- nab.wait()/nab.delay(). That is what makes a blocking call stop being a hole
-- in which nothing else happens.
--
-- Resident in the boot chunk (flash) rather than REPL-loaded: it is a core
-- runtime service, and an app that has to load its own scheduler before it can
-- keep an ear on target is not much of a fix.
--
-- Time is the wrapping 32-bit nab.time(); deadlines are compared as
-- (now - due) >= 0 so the comparison stays correct across the wrap (Lua 5.4
-- integers wrap two's-complement, and LUA_32BITS makes them 32-bit, so this is
-- the usual signed-difference trick, good for intervals under ~24 days).

sched = {}
local pumps, tasks = {}, {}

-- Returns a handle. Without one there was no way to undo a registration, so
-- :attach() was not idempotent, a finished player/ears was stepped for the rest
-- of the session, the list grew unbounded (each closure pinning a whole player,
-- its queue and its 2 KB tail), and a raising pump had no recovery path (#297).
-- The function itself is the handle: unique per closure, and it costs nothing.
function sched.pump(fn)
  pumps[#pumps + 1] = fn
  return fn
end

-- Remove a pump by its handle. Removing one that is already gone is a no-op,
-- not an error: a :detach() must be safe to call from a stop() path and from a
-- finaliser without either having to know which ran first.
--
-- Returns the index it removed from, or false if the handle was not registered
-- (both truthy/falsy as before - no caller reads more than that). sched.tick
-- needs the index to keep its cursor aligned when it drops a pump (#309).
function sched.unpump(h)
  for i = 1, #pumps do
    if pumps[i] == h then
      table.remove(pumps, i)
      return i
    end
  end
  return false
end

function sched.spawn(fn)
  local t = {co = coroutine.create(fn), due = nab.time()}
  tasks[#tasks + 1] = t
  return t.co
end

function sched.sleep(ms)
  coroutine.yield(nab.time() + ms)
end

-- The app's own "tick" callback, if it registered one. See the nab.on wrapper.
local app_tick

-- Report a broken activity without taking the runtime down (principle 3).
local function blame(err)
  print("sched: " .. tostring(err))
end

-- One slice: every pump, then every task whose deadline has come, then the
-- app's tick. A pump or task that errors is dropped and the rest keep running.
--
-- The pumps used to be called bare while the tasks were wrapped in
-- coroutine.resume, so one raising pump aborted the slice at its index - every
-- later pump and every task skipped - and, never being removed, repeated that
-- on every pump iteration forever (#297). Both halves are protected now, and
-- both drop the offender.
function sched.tick()
  local i = 1
  while i <= #pumps do
    local fn = pumps[i]
    local ok, err = pcall(fn)
    if not ok then
      -- Drop the offender by IDENTITY, not by index (#309). `table.remove(pumps, i)`
      -- assumed the raising pump was still at i; if it had removed another pump
      -- first, i pointed at an innocent one - which then got dropped while the
      -- offender stayed registered and raised again on every following tick.
      -- Removing from BELOW the cursor shifts everything down with it, so the
      -- cursor follows, or the pump that moved into the vacated slot loses its
      -- slice.
      blame(err)
      local k = sched.unpump(fn)
      if k and k < i then
        i = i - 1
      end
    end
    -- Advance only if the list did not shift under us. A pump may call
    -- sched.unpump on itself from inside its own body - :detach() from a
    -- finished :step() is the natural way to end an animation - and that
    -- table.remove pulls the NEXT pump down into index i, which a blind
    -- i = i + 1 would then step over for the rest of the slice. If fn is still
    -- at i it is ours to advance past; if it is not, i already holds a pump
    -- that has not run yet.
    if pumps[i] == fn then
      i = i + 1
    end
  end
  i = 1
  while i <= #tasks do
    local t = tasks[i]
    if (nab.time() - t.due) >= 0 then
      local ok, due = coroutine.resume(t.co)
      if not ok then
        blame(due)
      end
      if not ok or coroutine.status(t.co) == "dead" then
        table.remove(tasks, i)
        i = i - 1
      else
        t.due = due or nab.time()
      end
    end
    i = i + 1
  end
  if app_tick then
    local ok, err = pcall(app_tick)
    if not ok then
      blame(err)
      app_tick = nil        -- as for a pump: drop the offender
    end
  end
end

-- "tick" is the reactor's seam, not a free slot. The C nab.on holds ONE
-- callback per name and replaces it silently, so an app doing what the nab
-- module table documents - nab.on("tick", fn) - used to unhook sched.tick
-- outright: every :attach()ed ears and player stopped being stepped and every
-- spawned choreography froze, with no error and nothing to see it from (#297).
-- Route it through sched instead and leave sched.tick as the registered
-- callback. Everything else goes straight through.
local raw_on = nab.on

function nab.on(name, fn)
  if name == "tick" then
    app_tick = fn
    return
  end
  return raw_on(name, fn)
end

raw_on("tick", sched.tick)

-- Generational GC (#283). The allocation profile here is a stream of
-- short-lived strings - every parsed packet in net/ builds several - against a
-- 1 MB heap that lives in ExtRAM behind a 16-bit bus. Incremental mode keeps
-- traversing that whole heap; generational collects the nursery those strings
-- die in and only rarely walks everything, which is the difference between
-- predictable pacing and a multi-ms stall landing mid-choreography. Costs no
-- flash - Lua 5.4 already has both collectors compiled in.
collectgarbage("generational")

GREEN_UID = "d0021a3506198b86"
YELLOW_UID = "d0021a35038f3a2f"
LEFT_MOTOR = 1
RIGHT_MOTOR = 2

function allled(r, g, b)
  nab.led('nose', r, g, b)
  nab.led('belly', r, g, b)
  nab.led('left', r, g, b)
  nab.led('right', r, g, b)
  nab.led('bottom', r, g, b)
end

function greenmode()
  allled(0, 127, 0)
  nab.ear_move(LEFT_MOTOR, 'forward')
end

function yellowmode()
  allled(127, 127, 0)
  nab.ear_move(RIGHT_MOTOR, 'forward')
end

function colormode()
  nab.led('nose', 127, 0, 0)
  nab.led('belly', 127, 127, 0)
  nab.led('left', 0, 127, 0)
  nab.led('right', 0, 0, 127)
  nab.led('bottom', 127, 0, 127)
end

function blackmode()
  allled(0, 0, 0)
  nab.led('bottom', 0, 0, 127)
  nab.ear_stop(LEFT_MOTOR)
  nab.ear_stop(RIGHT_MOTOR)
end

function react(t)
  if t == GREEN_UID then
    greenmode()
  elseif t == YELLOW_UID then
    yellowmode()
  else
    blackmode()
  end
end

-- Event-driven variant of run() (#195): register callbacks and return to the
-- REPL. react() then fires from the cooperative pump (REPL idle / nab.wait)
-- when a tag lands or leaves the coupler; a button press stops watching and
-- goes dark. No busy loop, and the prompt stays usable while it watches.
function watch()
  nab.on('rfid', react)
  nab.on('button', function(pressed)
    if pressed then
      nab.on('rfid', nil)
      nab.on('button', nil)
      blackmode()
    end
  end)
end

function run()
  while true do
    react(nab.rfid())
    if nab.ear_pos(LEFT_MOTOR) == nab.ear_pos(RIGHT_MOTOR) then
      colormode()
    end
    if nab.button() then
      blackmode()
      return
    end
  end
end

-- Short boot LED showcase (#102): breathe all five LEDs blue then magenta, then
-- run a ball round the belly ring. nab.fade runs in the background off the 1 ms
-- timer IRQ; nab.delay paces the frames off the same clock. Bounded (~6 s) so it
-- never strands the REPL (contrast run(), #207).
BELLY_RING = { 'belly', 'right', 'bottom', 'left' }
ALL_LEDS = { 'nose', 'belly', 'left', 'right', 'bottom' }

function fade5(r, g, b, ms)
  for i = 1, #ALL_LEDS do nab.fade(ALL_LEDS[i], r, g, b, ms) end
end

function ledshow()
  print('LED showcase (#102): breathe + ring')
  for i = 1, #ALL_LEDS do nab.led8(ALL_LEDS[i], 0, 0, 0) end
  fade5(0, 120, 230, 700); nab.delay(900)
  fade5(230, 0, 120, 700); nab.delay(900)
  fade5(0, 0, 0, 600); nab.delay(700)
  for i = 1, #BELLY_RING do
    nab.fade(BELLY_RING[i], 120, 170, 255, 80); nab.delay(150)
    nab.fade(BELLY_RING[i], 0, 0, 0, 430)
  end
  nab.delay(500); fade5(0, 0, 0, 300)
  print('LED showcase done')
end

blackmode() -- idle LED state; then the REPL. Type run() or ledshow() to start.
