-- One corruption candidate, in its own process so a crash is countable.
--
-- Reads the subject chunk, replaces byte <pos> with <val>, and puts it through
-- exactly what the device does with an #LC frame: load() with the chunk as
-- binary (the image has no other mode - lparser/llex are not linked), then a
-- pcall of whatever came back.
--
-- The verdict rides the EXIT CODE, not stdout: io.popen is not compiled into
-- this lua (no LUA_USE_POSIX), and os.execute reports a child's exit code and
-- signal death directly, which is exactly what the parent needs to tell a
-- refusal from a crash. The codes are deliberately not 0/1 - those are what
-- the interpreter itself exits with on success and on an uncaught error, and
-- the parent must be able to tell those apart from a real verdict.
--
-- Usage: lua load_one.lua <chunkfile> <pos> <byte>

local REJECTED, RAN, RTERR = 10, 11, 12

local path, pos, val = arg[1], tonumber(arg[2]), tonumber(arg[3])

local f = assert(io.open(path, "rb"))
local c = f:read("a")
f:close()

c = c:sub(1, pos - 1) .. string.char(val) .. c:sub(pos + 1)

local fn = load(c, "=corrupt", "b")
if not fn then
  os.exit(REJECTED)     -- the loader spotted it: the correct outcome
end

local ok = pcall(fn)
os.exit(ok and RAN or RTERR)   -- ran, or raised - both are contained
