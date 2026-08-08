-- Host-side unit-test runner for the resident boot chunk (boot.lua).
--
-- `sched` - the cooperative reactor everything on this track composes through
-- (#283) - lives in boot.lua and had no tests at all: it is the one piece of
-- resident Lua, it is what hw.ears/audio.player/net.iface hang their :step()
-- on, and until now the only thing exercising it was a whole-firmware
-- simulator run (lua:firmware:test:sched). It is pure Lua over three nab
-- primitives, so it unit-tests host-side in milliseconds - same shape as
-- lib/<lib>/test/run.lua, under the tools/luac host `lua` (same vendored tree
-- + LUA_32BITS luaconf.h as the device, so integer width and the 32-bit
-- wrap-around match the rabbit exactly).
--
-- Usage: lua boot/test/run.lua   (paths resolved relative to arg[0])

local base = arg[0]:match("^(.*)/test/[^/]+$") or "."

-- The vendored lbaselib drops dofile/loadfile (no filesystem on the device),
-- and the host lua is built from that same tree - so file execution goes
-- through io.open + load here.
local function runfile(path)
  local f = assert(io.open(path, "r"))
  local src = f:read("a")
  f:close()
  assert(load(src, "@" .. path))()
  return src
end

-- ---- the device seam boot.lua is written against ---------------------------
-- A faithful stand-in for src/main.c's `nab` module, not a convenient one:
-- every behaviour asserted below is a behaviour of the real bindings.
--
--  * nab.time() is the 1 ms tick (counter_timer). Here the test owns it, so
--    deadlines are exact and nothing depends on wall time.
--  * nab.on(name, fn|nil) holds ONE callback per name and replaces it
--    silently - utils/pump.c's pump_on is `lua_setfield(L, -2,
--    event_names[which])`, four lines with no chaining and no diagnostic.
--    Modelling that faithfully is the point: an app that registers its own
--    "tick" is exercising the documented API (see firmware/README.md's nab
--    module table).
--  * dispatch() below is utils/pump.c's pump_dispatch(), rule for rule -
--    including the busy guard, which this harness used NOT to model. That gap
--    is why the suite whose job is protecting the reactor could not see the
--    nested-nab.wait trap (#329): nab_pump() was a plain call and nab.wait()
--    was a no-op, so no test could tell a pump that keeps running from one
--    that silently stops. test_pump.lua is the part that would now fail if the
--    guard went away again.

NAB_CB = {}          -- the C-side callback table (registry key "nab.events")
local clock = 0

-- The modelled device's three pieces of state, matching the C ones:
--   timeline  the hardware, as a list of edges waiting for their moment. The
--             pollers in utils/event.c see an edge only when they run, so a
--             test injects WHEN a thing happens and the modelled poller is
--             what notices it - that is what makes "the pollers run even when
--             re-entered" an assertable rule rather than a comment.
--   queue     the fixed-size C event queue (utils/event.c), FIFO.
--   polls     how many times the pollers have run. The only way to tell rule 2
--             (sampling continues inside the guard) from rule 1 (delivery does
--             not) is to count them separately.
local timeline, queue, polls = {}, {}, 0
local busy = false   -- pump_dispatch's function-local `static uint8_t busy`

-- pump.c's pump_on: one callback per name, replaced silently. Kept as its own
-- local so boot_reload can put it back - boot.lua wraps nab.on to own the
-- "tick" seam, and re-running the chunk over its own wrapper would model a
-- device that booted twice, which is not a thing.
local function raw_on(name, fn) NAB_CB[name] = fn end

-- utils/event.c's event_pump(): look at the hardware, queue whatever edge has
-- happened since last time. Touches no Lua state, which is exactly why
-- pump_dispatch calls it OUTSIDE the busy guard.
local function event_pump()
  polls = polls + 1
  local i = 1
  while i <= #timeline do
    if (clock - timeline[i].at) >= 0 then
      queue[#queue + 1] = table.remove(timeline, i)
    else
      i = i + 1
    end
  end
end

-- utils/pump.c's pump_dispatch(), rule for rule. Returns the error message
-- when a callback raised (the last one, if several did), else nil - the
-- harness's stand-in for luaseam_report printing it to the console.
local function dispatch()
  event_pump()               -- rule 2: ALWAYS, even when re-entered
  if busy then return nil end -- rule 1: nested calls deliver nothing
  busy = true
  local err
  while #queue > 0 do        -- rule 3: drain the queue first...
    local e = table.remove(queue, 1)
    local fn = NAB_CB[e.name]
    if fn then
      local okc, e2 = pcall(fn, e.arg)   -- rule 4: pcall isolation
      if not okc then err = tostring(e2) end
    end
  end
  local fn = NAB_CB.tick     -- ...then hand the Lua reactor its slice
  if fn then
    local okc, e2 = pcall(fn)
    if not okc then err = tostring(e2) end
  end
  busy = false
  return err
end

nab = {
  time = function() return clock end,
  on = raw_on,
  -- nab.wait(ms) / nab.delay(ms): main.c's nab_wait - dispatch once, then
  -- advance the 1 ms tick dispatching at every step. At top level that is what
  -- makes a blocking call stop being a hole; from INSIDE a callback every one
  -- of those dispatches hits the busy guard, so time passes and the pollers
  -- keep sampling while nothing at all is delivered. Both halves are the
  -- device's, and test_pump.lua asserts them.
  wait = function(ms)
    ms = ms or 0
    dispatch()
    for _ = 1, ms do
      clock = clock + 1
      dispatch()
    end
  end,
  -- everything boot.lua's demo helpers touch at load time; no-ops here
  led = function() end,
  led8 = function() end,
  fade = function() end,
  ear_move = function() end,
  ear_stop = function() end,
  ear_pos = function() return 0 end,
  button = function() return false end,
  rfid = function() return nil end,
}
nab.delay = nab.wait   -- the C table registers nab_wait under both names

-- test-side control of the modelled device ------------------------------------

function nab_set_time(ms) clock = ms end
function nab_advance(ms) clock = clock + ms end

-- Schedule a hardware edge: name is "button" (arg true/false) or "rfid" (arg a
-- uid string, or nil for "the tag left"), at the clock reading when the
-- pollers should first be able to see it.
function nab_inject(at, name, arg)
  timeline[#timeline + 1] = {at = at, name = name, arg = arg}
end

-- How many times the pollers have run. Rule 2 is only observable as a count.
function nab_polls() return polls end

-- Clear the modelled hardware and its counters; boot_reload calls it, so each
-- scenario starts from a device with nothing pending.
function nab_reset_hw()
  timeline, queue, polls, busy = {}, {}, 0, false
end

-- One iteration of the cooperative pump, from the top level - what the REPL's
-- idle loop does between lines. Returns the error message when a callback
-- raised, else nil.
function nab_pump()
  return dispatch()
end

-- ---- load the chunk under test ---------------------------------------------
-- boot.lua is the real device source, loaded verbatim: no copy to drift.

local BOOT = base .. "/boot.lua"
local src = runfile(BOOT)

-- sched keeps its pump/task lists in upvalues with no reset hook (it is a
-- resident singleton on the device - there is nothing to reset it *to*), so a
-- scenario that wants a clean reactor re-runs the chunk. Clears the modelled
-- callback table too, so each scenario starts from a bare device.
function boot_reload()
  NAB_CB = {}
  nab.on = raw_on      -- a fresh device: nab.on is the C binding
  nab_reset_hw()       -- ...with nothing queued and nothing pending
  runfile(BOOT)
end

-- The device opens base + table + string + coroutine only (src/main.c's
-- loadedlibs). Unlike lua/lib/ (which must stay coroutine-free so it can run
-- under either), the resident chunk IS the coroutine user - sched.spawn is
-- built on it - so coroutine is allowed here and the rest is not.
local FORBIDDEN = {"%f[%w]math%.", "%f[%w]os%.", "%f[%w]io%.",
                   "%f[%w]package%.", "%f[%w]debug%.", "%f[%w]utf8%.",
                   "%f[%w]require%f[%W]", "%f[%w]dofile%f[%W]",
                   "%f[%w]loadfile%f[%W]"}
do
  local code = src:gsub("%-%-[^\n]*", "") -- comments may cite anything
  for _, pat in ipairs(FORBIDDEN) do
    assert(not code:find(pat),
           "boot.lua uses " .. pat .. " - not in the device stdlib")
  end
end

-- helpers available to every test file ---------------------------------------

local passed, failed = 0, 0

function eq(got, want, label)
  if got == want then
    passed = passed + 1
  else
    failed = failed + 1
    local function s(v)
      if type(v) == "string" then return ("%q"):format(v) end
      return tostring(v)
    end
    print(("FAIL %s: got %s, want %s"):format(label, s(got), s(want)))
  end
end

function ok(cond, label) eq(not not cond, true, label) end

-- ---- run every test_*.lua ---------------------------------------------------

for _, m in ipairs({"sched", "pump"}) do
  runfile(base .. "/test/test_" .. m .. ".lua")
end

print(("boot tests: %d passed, %d failed"):format(passed, failed))
if failed > 0 or passed == 0 then os.exit(1) end
