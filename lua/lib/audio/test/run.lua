-- Host-side unit-test runner for lua/audio (#265): pure-Lua modules driven
-- against fake HAL/network drivers, run under the tools/luac host `lua` (same
-- vendored tree + LUA_32BITS luaconf.h as the device, so integer width and
-- string.pack semantics match the rabbit exactly).
--
-- Every test asserts positive expected content per the repo testing rule -
-- MIDI files are compared against bytes derived from the SMF spec by hand, and
-- the player/stream tests assert what the *decoder* received, not just that
-- the module believes it played something.
--
-- Usage: lua audio/test/run.lua   (paths resolved relative to arg[0])

local base = arg[0]:match("^(.*)/test/[^/]+$") or "."

-- The vendored lbaselib drops dofile/loadfile (no filesystem on the device),
-- and the host lua is built from that same tree - so file execution goes
-- through io.open + load here.
function RUNFILE(path)
  local f = assert(io.open(path, "r"))
  local src = f:read("a")
  f:close()
  assert(load(src, "@" .. path))()
  return src
end

-- audio/ modules under test; LIBDIR lets a test pull in a sibling lib it
-- composes with (audio.stream parses with net.http).
LIBDIR = base:match("^(.*)/[^/]+$") or ".."
MODULES = {"player", "stream", "midi", "volume"}

-- The device opens base + table + string only (src/main.c): fail fast if a
-- module drifts onto host-only stdlib. Word-boundary match keeps e.g.
-- "date.iso" or a local named "iov" from tripping it.
local FORBIDDEN = {"%f[%w]math%.", "%f[%w]os%.", "%f[%w]io%.",
                   "%f[%w]coroutine%.", "%f[%w]package%.", "%f[%w]debug%.",
                   "%f[%w]require%f[%W]", "%f[%w]dofile%f[%W]",
                   "%f[%w]load%f[%W]"}
for _, m in ipairs(MODULES) do
  local f = io.open(base .. "/" .. m .. ".lua", "r")
  if f then
    f:close()
    local src = RUNFILE(base .. "/" .. m .. ".lua")
    src = src:gsub("%-%-[^\n]*", "") -- comments may cite anything
    for _, pat in ipairs(FORBIDDEN) do
      assert(not src:find(pat),
             m .. ".lua uses " .. pat .. " - not in the device stdlib")
    end
  end
end

-- helpers available to every test file ---------------------------------------

function H(hex) -- "aabb cc" -> binary string
  return (hex:gsub("%s", ""):gsub("%x%x",
          function(b) return string.char(tonumber(b, 16)) end))
end

function X(s) -- binary string -> "aabbcc"
  return (s:gsub(".", function(c) return ("%02x"):format(c:byte()) end))
end

local passed, failed = 0, 0

function eq(got, want, label)
  if got == want then
    passed = passed + 1
  else
    failed = failed + 1
    local function s(v)
      if type(v) == "string" then return ("%q"):format(v):gsub("\\\n", "\\n") end
      return tostring(v)
    end
    print(("FAIL %s: got %s, want %s"):format(label, s(got), s(want)))
  end
end

function ok(cond, label) eq(not not cond, true, label) end

-- A fake VS1003: records everything fed to it and accepts only `rate` bytes
-- per feed, so the tests see the real short-return flow control. `busy` models
-- the decoder still chewing (SCI_HDAT1 != 0) for `drain` steps after the last
-- byte. Time is a plain counter the test advances.
function FAKEDEC(o)
  o = o or {}
  local d = {fed = {}, nfed = 0, started = 0, stopped = 0, t = 0,
             rate = o.rate or 32, left = o.drain or 0, open = false}

  d.drv = {
    start = function() d.started = d.started + 1; d.open = true end,
    stop  = function() d.stopped = d.stopped + 1; d.open = false end,
    time  = function() return d.t end,
    sleep = function() end,
    feed  = function(s, i)
      if not d.open or d.rate == 0 then return 0 end
      local chunk = s:sub(i, i + d.rate - 1)
      d.fed[#d.fed + 1] = chunk
      d.nfed = d.nfed + #chunk
      d.left = o.drain or 0
      return #chunk
    end,
    busy  = function()
      if d.left > 0 then d.left = d.left - 1; return true end
      return false
    end,
  }
  function d:bytes() return table.concat(self.fed) end
  return d
end

-- run every test_*.lua that exists for a landed module -----------------------

for _, m in ipairs(MODULES) do
  local path = base .. "/test/test_" .. m .. ".lua"
  local f = io.open(path, "r")
  if f then
    f:close()
    RUNFILE(path)
  end
end

print(("audio tests: %d passed, %d failed"):format(passed, failed))
if failed > 0 or passed == 0 then os.exit(1) end
