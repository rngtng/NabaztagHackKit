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
