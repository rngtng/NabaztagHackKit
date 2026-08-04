-- Robustness of the bytecode loading path - the ONE way anything gets into
-- this firmware (#128).
--
-- The image has no parser, so `luaU_undump` (lua/lundump.c, vendored PUC-Rio
-- 5.4.7 verbatim) is the entire input surface: every `#LC` frame typed at the
-- console or piped by tools/luac, the resident boot chunk, and - once design
-- principle 4's script slots land - every payload loaded remotely. This
-- harness corrupts a chunk one byte at a time and requires the loader to
-- refuse it or run it, never to die.
--
-- Why this is not theoretical:
--
--   * The transport half is closed (#298): the `#LC` frame now carries a
--     Fletcher-32 that load_lc_frame verifies before luaL_loadbuffer sees the
--     buffer, so a chunk damaged on the wire is refused rather than loaded.
--     What that does NOT cover is a chunk that arrives intact and is simply
--     malformed - a truncated file, a stale .lc, a payload from a source the
--     checksum cannot speak for. That is what this measures.
--   * PUC-Rio does not claim otherwise. The manual on `load`: "Lua does not
--     check the consistency of binary chunks. Maliciously crafted binary chunks
--     can crash the interpreter." lundump.c reads sizes and indices straight
--     out of the stream and allocates/indexes on them.
--   * On the host a bad chunk segfaults. On the rabbit there is no MMU: the
--     same wild access lands in the 1 MB ExtRAM heap or the flash below it, so
--     the failure mode is silent corruption, not a clean stop.
--
-- firmware/README.md's principle 5 reads the situation the other way round -
-- "the parser-less image hardens this further: with no on-device compiler the
-- rabbit cannot eval source, only run bytecode it is handed". Dropping the
-- parser removed a *hardened* front end (llex/lparser reject malformed input by
-- construction; that is what a parser is) and left an unhardened one. The
-- sandbox argument is sound for *source*, and inverted for *bytecode*.
--
-- Fixes, cheapest first, and they are not exclusive:
--   1. A checksum in the `#LC` frame - DONE (#298).
--   2. Validate what lundump reads before trusting it - the declared sizes,
--      the constant type tags, register/upvalue counts against the Proto.
--   3. For principle 4's remote slots, authenticate the payload; a checksum is
--      not a signature.
-- 2 and 3 are open decisions, tracked in #305; this run's numbers are the
-- evidence they should be taken on.
--
-- Run under the tools/luac host `lua`, built from this same vendored tree with
-- the same LUA_32BITS luaconf.h - so the undump layout, integer width and
-- float width are the device's. Each candidate runs in a child process, so a
-- crash is a countable result rather than the end of the run.
--
-- Usage: lua firmware/test/bytecode/run.lua [--full]

local base = arg[0]:match("^(.*)/[^/]+$") or "."
local FULL = arg[1] == "--full"

-- The subject: a chunk with the shapes a real script has - locals, upvalues,
-- a nested function, constants of more than one type, a call and a return.
-- Kept small so the sweep stays quick; --full widens the value set.
local SOURCE = [[
local n, s = 7, "hi"
local function f(x) return x + n, s .. tostring(x) end
local t = {f(1)}
return t[1], t[2]
]]

local CHUNK = string.dump(assert(load(SOURCE, "=subject")), true)

-- Written once; the child re-reads it and applies its own corruption, so the
-- parent never has to pass binary through a shell argument.
local BIN = os.tmpname()
do
  local f = assert(io.open(BIN, "wb"))
  f:write(CHUNK)
  f:close()
end

-- The child: load the corrupted chunk as binary only ("b" - the device has no
-- other mode) and, if it loads, run it under pcall. It reports through its
-- exit code (io.popen is not compiled into this lua). Anything that is not one
-- of the three verdicts - a signal, or an exit code we did not choose - is the
-- loader taking the process down with it.
local CHILD = base .. "/load_one.lua"
local VERDICT = {[10] = "REJECTED", [11] = "RAN", [12] = "RTERR"}

local LUA = arg[-1] or "lua"

-- os.execute's third return is only decoded into an exit code / signal number
-- on a POSIX build (loslib.c's l_inspectstat is a no-op otherwise), and this
-- lua is not one - it hands back the raw wait status, i.e. code << 8. Decode it
-- here rather than trusting `how`, which is then always "exit". A child killed
-- by a signal also reaches us as the shell's 128+signal convention, so treat
-- anything >= 128 as a death too.
local function classify(pos, val)
  local cmd = ("%s %q %q %d %d >/dev/null 2>&1")
              :format(LUA, CHILD, BIN, pos, val)
  local _, how, st = os.execute(cmd)
  if how == "signal" then return "crash" end
  local code = st
  if type(code) == "number" and code > 255 then code = code >> 8 end
  if type(code) ~= "number" or code >= 128 then return "crash" end
  return VERDICT[code] or "crash"
end

local VALUES = FULL and {0, 1, 2, 0x7F, 0x80, 0xFE, 0xFF}
                    or {0, 1, 0x7F, 0xFF}

local counts = {crash = 0, REJECTED = 0, RAN = 0, RTERR = 0}
local crashes = {}

for pos = 1, #CHUNK do
  for _, val in ipairs(VALUES) do
    if CHUNK:byte(pos) ~= val then
      local verdict = classify(pos, val)
      counts[verdict] = (counts[verdict] or 0) + 1
      if verdict == "crash" then
        crashes[#crashes + 1] = ("byte %d -> 0x%02X"):format(pos, val)
      end
    end
  end
end

os.remove(BIN)

local total = 0
for _, n in pairs(counts) do total = total + n end

print(("bytecode-robustness: %d corrupted chunks of a %d-byte subject")
      :format(total, #CHUNK))
print(("  refused by the loader : %d"):format(counts.REJECTED))
print(("  loaded and ran        : %d"):format(counts.RAN))
print(("  loaded, raised at run : %d"):format(counts.RTERR))
print(("  KILLED THE PROCESS    : %d"):format(counts.crash))

-- Guard against a vacuous pass: if the subject stopped being loadable at all,
-- or the child stopped running it, every candidate would be "REJECTED" and the
-- crash count would be zero for the wrong reason.
if counts.RAN == 0 then
  print("bytecode-robustness: no corrupted chunk ran - the harness is not "
        .. "exercising the loader ✗")
  os.exit(1)
end
if counts.REJECTED == 0 then
  print("bytecode-robustness: nothing was refused - the child is not loading "
        .. "as binary ✗")
  os.exit(1)
end

-- The ratchet (#305). The loader may not get LESS strict than it is today:
-- `refused` is deterministic - it is lundump's own validation - so it is what
-- this gate asserts. Harden the loader and it climbs; weaken it and this fails.
--
-- The crash count is REPORTED, not asserted. #298 fixed the transport half of
-- this surface (a frame damaged in flight is now refused before the loader sees
-- it) and deliberately did not touch lundump, so a residual is expected. It also
-- wobbles by a few between runs: whether a wild access faults at all depends on
-- process layout, so pinning it would be pinning noise. #305 is where the
-- hardening decision gets made; raise BASELINE_REFUSED in the same commit.
local BASELINE_REFUSED = 188

if counts.crash > 0 then
  print(("\n%d corrupted chunks killed the runtime. That is the residual #298 "
         .. "left: it fixed the transport, not lundump. On this target it is "
         .. "not a segfault - there is no MMU, so the same access silently "
         .. "corrupts the ExtRAM heap. Tracked in #305."):format(counts.crash))
  print("First few:")
  for i = 1, math.min(#crashes, 6) do print("  " .. crashes[i]) end
  if #crashes > 6 then print(("  ... and %d more"):format(#crashes - 6)) end
end

if counts.REJECTED < BASELINE_REFUSED then
  print(("\nbytecode-robustness: the loader refused %d chunks, down from the "
         .. "%d baseline - it has become LESS strict ✗")
        :format(counts.REJECTED, BASELINE_REFUSED))
  os.exit(1)
end

print(("bytecode-robustness: loader refused %d >= %d baseline ✓")
      :format(counts.REJECTED, BASELINE_REFUSED))
