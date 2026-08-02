-- net.ota: firmware-image verification + portal upload (#235). The whole point
-- is that nothing reaches flash unverified, so every abort path is asserted to
-- leave the fake flasher untouched, and the happy path to hand it the exact
-- image bytes. CRC checked against the standard check value, not self-derived.

local ota = net.ota

-- build a .fw blob the way tools/otaimage.py does (independent of ota.verify)
local function fw(image, o)
  o = o or {}
  return string.pack(">c4BBI2I4I4", ota.MAGIC, o.hver or 1, o.hw or ota.HW_ID,
                     o.ver or 3, o.len or #image, o.crc or ota.crc32(image))
         .. image
end

local IMG = H"0102030405" .. ("\255"):rep(7) .. H"deadbeef" -- 16 bytes, mixed

-- crc32 matches the standard --------------------------------------------------

eq(ota.crc32("123456789"), 0xCBF43926, "crc32 hits the standard check value")
eq(ota.crc32(""), 0, "crc32 of empty is 0")
eq(ota.crc32("a") ~ 0, ota.crc32("a"), "crc32 stays a 32-bit integer")

-- verify: the happy path -----------------------------------------------------

local img, meta = ota.verify(fw(IMG))
eq(img, IMG, "verify returns the exact image bytes")
eq(meta and meta.len, #IMG, "meta carries the image length")
eq(meta.version, 3, "meta carries the firmware version")
eq(meta.crc, ota.crc32(IMG), "meta carries the image crc")

-- verify: every rejection leaves no doubt -----------------------------------

eq((ota.verify("NBZ")), nil, "a runt file is rejected")
ok(select(2, ota.verify(("x"):rep(20))):find("magic"), "bad magic named in error")
ok(select(2, ota.verify(fw(IMG, {hw = 2}))):find("hardware"),
   "wrong hardware id rejected")
ok(select(2, ota.verify(fw(IMG, {hver = 2}))):find("header version"),
   "unsupported header version rejected")
ok(select(2, ota.verify(fw(IMG, {len = #IMG + 5}))):find("length"),
   "length mismatch rejected")
ok(select(2, ota.verify(fw(IMG, {crc = 0xDEADBEEF}))):find("checksum"),
   "crc mismatch rejected")
ok(select(2, ota.verify(fw(IMG, {len = ota.MAX_IMAGE + 1}))):find("budget"),
   "an over-budget image is rejected before the length check")

-- verify: caller can override the expected hardware id -----------------------

eq((ota.verify(fw(IMG, {hw = 7}), {hw_id = 7})), IMG,
   "opts.hw_id lets a different target verify")

-- apply: flashes only a verified image ---------------------------------------

local flashed
local function fakeflash(bytes) flashed = bytes end

flashed = nil
local m = ota.apply(fw(IMG), nil, {flash = fakeflash})
eq(flashed, IMG, "apply flashes the exact verified image")
eq(m and m.version, 3, "apply returns the meta on success")

flashed = nil
local ok2, err = ota.apply(fw(IMG, {crc = 1}), nil, {flash = fakeflash})
eq(ok2, nil, "apply aborts on a bad image")
ok(err:find("checksum"), "apply surfaces the verify error")
eq(flashed, nil, "apply flashes NOTHING when verification fails")

-- handle: the portal route ---------------------------------------------------

local body, status, stop = ota.handle({method = "GET"})
ok(body:find("type=file"), "GET serves the upload form")
ok(body:find("/firmware"), "upload form posts to /firmware")
ok(body:find("Do not unplug"), "upload page carries the brick-safety warning")
eq(stop, nil, "GET does not stop the portal")

local pbody, pstatus, pstop, pimg = ota.handle({method = "POST", body = fw(IMG)})
eq(pstop, true, "a verified upload stops the portal")
eq(pimg, IMG, "handle returns the verified image for the caller to flash")
ok(pbody:find("Flashing"), "verified upload shows the flashing page")
ok(pbody:find("Do not unplug"), "flashing page repeats the warning")

local bbody, bstatus, bstop, bimg = ota.handle({method = "POST", body = "junk"})
eq(bstop, nil, "a bad upload does not stop the portal")
eq(bimg, nil, "a bad upload yields no image to flash")
ok(bbody:find("Upload failed"), "a bad upload re-serves the form with the error")

-- verifying a real-sized image must be cheap enough, and interruptible -------
--
-- ota.verify's cost is ota.crc32, and ota.crc32 is a bit-at-a-time CRC-32:
-- eight iterations of shift/xor/and/negate per byte. On a real 117,516-byte
-- image that is measured below at ~6.7M Lua VM instructions - on a 32 MHz
-- ARM7TDMI whose heap sits in ExtRAM behind a 16-bit bus, seconds of work, not
-- milliseconds. Two things follow, and both matter on the one path this code
-- exists for: a phone POSTing firmware to the setup portal.
--
--   1. It is ~5x more work than it needs to be. The standard byte-table form
--      of the same polynomial costs ~1.3M VM instructions for the identical
--      result, and the 256-entry table is built once into the heap - this lib
--      is loaded as bytecode over the REPL, so it costs no flash at all.
--   2. It is one uninterruptible call. iface:serve calls the handler, the
--      handler calls ota.verify, and nothing polls the interface, feeds the
--      decoder, steps an ear or drains an event until it returns. That is
--      exactly the hole #283 closed for nab.wait/nab.play/nab.wifi_recv, still
--      open here - and it sits between the phone's upload and its response,
--      with the phone's TCP retransmitting into a stack that is not listening.
--      Chunking the loop costs nothing measurable (~1.298M vs ~1.297M for
--      4 KB slices) and yields ~29 places to let the caller run.
--
-- Both assertions are deterministic: the budget is counted in VM instructions
-- via debug.sethook, not wall time, so it does not depend on how loaded the
-- machine is. Test-side use of debug is fine - the FORBIDDEN lint in run.lua
-- guards the MODULE sources, which is what has to fit the device stdlib.

local FW_BYTES = 117516        -- the real bin/firmware.elf image size
local BIG = string.rep("\165\90\1\254", FW_BYTES // 4)

local function vm_instructions(fn, ...)
  local n = 0
  debug.sethook(function() n = n + 1 end, "", 1000)
  fn(...)
  debug.sethook()
  return n * 1000
end

-- Sanity first, so the budget below cannot pass by measuring nothing: the
-- shipped crc32 must still be correct on a buffer this size.
local BIG_CRC = ota.crc32(BIG)
eq(BIG_CRC, ota.crc32(BIG:sub(1, #BIG)), "crc32 is stable on a full-size image")
ok(BIG_CRC ~= 0, "crc32 of the full-size fixture is a real value")

local cost = vm_instructions(ota.crc32, BIG)
ok(cost > 100000, "the VM-instruction probe actually measured the hash")
ok(cost <= 2500000,
   ("verifying a %d-byte image costs %d VM instructions, budget 2500000")
   :format(FW_BYTES, cost))

-- ...and it must hand the caller the reactor back while it works. Behaviour,
-- not mechanism: any bounded-slice form passes, whether that is a callback
-- here or a stepper object underneath it.
local pumped = 0
ota.verify(fw(BIG), {pump = function() pumped = pumped + 1 end})
ok(pumped >= 8,
   ("verify called the caller's pump %d times over %d bytes; a blocking hash "
    .. "calls it 0"):format(pumped, FW_BYTES))
