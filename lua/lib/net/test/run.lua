-- Host-side unit-test runner for lua/net (#217): pure-Lua modules on byte
-- strings, run under the tools/luac host `lua` (same vendored tree +
-- LUA_32BITS luaconf.h as the device, so integer width and string.pack
-- semantics match the rabbit exactly).
--
-- Fixture packets come from an independent Python implementation
-- (checksums cross-checked, not self-derived); every test asserts positive
-- expected content per the repo testing rule - never just "A == B".
--
-- Usage: lua net/test/run.lua   (paths resolved relative to arg[0])

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

MODULES = {"link", "arp", "ipv4", "udp", "dhcp", "tcp", "http", "iface"}

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
    local src = runfile(base .. "/" .. m .. ".lua")
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

-- run every test_*.lua that exists for a landed module -----------------------

for _, m in ipairs(MODULES) do
  local path = base .. "/test/test_" .. m .. ".lua"
  local f = io.open(path, "r")
  if f then
    f:close()
    runfile(path)
  end
end

print(("net tests: %d passed, %d failed"):format(passed, failed))
if failed > 0 or passed == 0 then os.exit(1) end
