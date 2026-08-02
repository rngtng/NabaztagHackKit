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
--    silently - main.c's nab_on is `lua_setfield(L, -2, event_names[which])`,
--    four lines with no chaining and no diagnostic. Modelling that faithfully
--    is the point: an app that registers its own "tick" is exercising the
--    documented API (see firmware/README.md's nab module table).
--  * dispatching a queued event and then the "tick" callback is what
--    main.c's dispatch_events() does on every pump iteration.

NAB_CB = {}          -- the C-side callback table (registry key "nab.events")
local clock = 0

nab = {
  time = function() return clock end,
  on = function(name, fn) NAB_CB[name] = fn end,
  -- everything boot.lua's demo helpers touch at load time; no-ops here
  led = function() end,
  led8 = function() end,
  fade = function() end,
  ear_move = function() end,
  ear_stop = function() end,
  ear_pos = function() return 0 end,
  button = function() return false end,
  rfid = function() return nil end,
  delay = function() end,
  wait = function() end,
}

-- test-side control of the modelled device ------------------------------------

function nab_set_time(ms) clock = ms end
function nab_advance(ms) clock = clock + ms end

-- One iteration of main.c's cooperative pump: deliver the tick slice under
-- pcall, exactly as dispatch_events() does (`lua_pcall` + report on error, then
-- carry on). Returns the error message when the slice raised, else nil.
function nab_pump()
  local fn = NAB_CB.tick
  if fn == nil then return nil end
  local ok, err = pcall(fn)
  if not ok then return tostring(err) end
  return nil
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

for _, m in ipairs({"sched"}) do
  runfile(base .. "/test/test_" .. m .. ".lua")
end

print(("boot tests: %d passed, %d failed"):format(passed, failed))
if failed > 0 or passed == 0 then os.exit(1) end
