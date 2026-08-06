-- Hardware acceptance run: does this board still do what it did last time?
--
-- One flash of the PRODUCT image, one scripted pass over the nab.* seam. This
-- is deliberately NOT part of lua:verify - it needs the JTAG rig - so it is an
-- on-demand regression check:
--
--   task lua:firmware:test:hw
--
-- WHY THIS AND NOT THE PROBES. examples/*.c each prove one subsystem, but each
-- is a separate firmware image, so covering them all costs a flash apiece and
-- none of them exercises the image actually shipped. Everything below is
-- asserted against the product firmware in a single run. The probes stay as the
-- layer beneath: this says "audio regressed", audioprobe says "the codec
-- answers but will not decode". Probes whose oracle is a human sense or an
-- instrument (uartcal wants a scope, ledmap wants a photograph, volprobe wants
-- ears) cannot become assertions and are not represented here.
--
-- The UART ladder needs no check at all: this script arrived over RX and its
-- transcript leaves over TX, so a completed run has already proven both.
--
-- ONE CHUNK, NOT PER-LINE - same reason as test/sched-demo.lua. replpipe.py
-- frames a *.lua file one chunk per source line, which would break every
-- multi-line function below; the task compiles this to .lc first.

-- Set to the UID of the tag fixed to the rig to pin it exactly, e.g.
-- "a1b2c3d4e5f60708". nil = accept any well-formed UID (still proves the I2C
-- bus, the CRX14 coupler and tag decode - only not *which* tag).
local EXPECT_UID = nil

local pass, total = 0, 0

local function check(name, ok, detail)
  total = total + 1
  if ok then
    pass = pass + 1
    print(string.format("  ok   %-28s %s", name, detail or ""))
  else
    print(string.format("  FAIL %-28s %s", name, detail or ""))
  end
end

-- Guard against a vacuous pass: a boot hang produces an empty transcript, and
-- the task requires this marker before it believes any verdict below.
print("<<HW-ACCEPT start>>")

-- 1. Audio codec alive on SPI0. SS_VER is bits 6:4 of SCI_STATUS; VS1003 = 3.
--    (examples/audioprobe.c reads exactly this.)
local status = nab.sci(0x01)
local ver = (status >> 4) & 0x0F
check("codec SS_VER == 3", ver == 3,
      string.format("SCI_STATUS=0x%04x ver=%d", status, ver))

-- 2. SCI write/read-back holds. The original value is restored so a run does
--    not leave the codec muted.
local vol0 = nab.sci(0x0B)
nab.sciw(0x0B, 0x2A2A)
local volrb = nab.sci(0x0B)
nab.sciw(0x0B, vol0)
check("codec SCI write/read-back", volrb == 0x2A2A,
      string.format("wrote 0x2a2a read 0x%04x", volrb))

-- Wheel: REPORTED, NOT ASSERTED - on purpose. adc_read_ch2() returns uint8_t,
-- so "in 0..255" holds by construction and could never fail; and a passive read
-- cannot separate a dead ADC from a wheel left turned to zero, because travel
-- runs 255 -> 0 -> 255 and 0 is a real position. Any honest check needs someone
-- to turn it, which puts it in the button's category. examples/gpioprobe.c is
-- the real check. ~255 is rest.
print(string.format("  --   %-28s wheel=%s (not asserted - needs a human)",
                    "wheel ADC", tostring(nab.wheel())))

-- 4. The 1 ms tick advances. Bounded above too: a wild jump means the timer
--    reload or the clock changed, which is a regression even though it moved.
local t0 = nab.time()
nab.wait(100)
local dt = nab.time() - t0
check("1 ms tick advances", dt > 0 and dt < 5000, string.format("dt=%dms", dt))

-- 5. Ear motor + encoder. The encoder is what makes this assertable without a
--    human: if the count moved, the motor turned. Raw wrapping 16-bit count, so
--    compare inequality, never magnitude.
local p0 = nab.ear_pos(1)
nab.ear_move(1, "forward")
nab.wait(400)
nab.ear_stop(1)
local p1 = nab.ear_pos(1)
check("ear 1 encoder moves", p1 ~= p0, string.format("%d -> %d", p0, p1))

-- 6. RFID. One nab.rfid() is a single live scan, so retry a few times before
--    calling it dead - a tag is expected to be fixed to the rig.
local uid = nil
for _ = 1, 5 do
  uid = nab.rfid()
  if uid then break end
  nab.wait(300)
end
local uid_ok = type(uid) == "string" and #uid == 16 and uid:match("^%x+$") ~= nil
if uid_ok and EXPECT_UID then
  uid_ok = (uid == EXPECT_UID)
end
check("rfid reads the rig tag", uid_ok,
      string.format("uid=%s%s", tostring(uid),
                    EXPECT_UID and (" expected=" .. EXPECT_UID) or ""))

-- 7. Radio enumerated: USB host + OHCI + RT2501 all had to work for the MAC to
--    be real. It is all-zero until wifi_up() cold-boots the dongle.
nab.wifi_up()
local mac = nab.wifi_mac()
local mac_ok = type(mac) == "string" and #mac == 6 and mac ~= string.rep("\0", 6)
local machex = ""
if type(mac) == "string" then
  for i = 1, #mac do machex = machex .. string.format("%02x", mac:byte(i)) end
end
check("wifi MAC is real", mac_ok, "mac=" .. machex)

-- 8. Scan finds at least one AP. NOTE: this is the one check that depends on
--    the room rather than the board - see the failure text.
local n = nab.wifi_scan()
local seen = nab.wifi_seen()
check("wifi scan sees >= 1 AP", type(n) == "number" and n >= 1,
      string.format("count=%s seen=%d%s", tostring(n),
                    type(seen) == "table" and #seen or 0,
                    (type(n) == "number" and n >= 1) and ""
                      or "  <- ENVIRONMENT, not necessarily the board: needs a"
                         .. " powered AP in range of the rig"))

-- 9. Config sector reads back. A rewrite of the identical record returns false
--    ("already identical"), which exercises the read + compare + verify path
--    without touching stored credentials. The full write path still needs the
--    manual write / power-cycle / read-back test - no automated run can cycle
--    the power.
local cfg = nab.config()
if cfg == nil then
  check("config sector readable", true, "no record stored (not a fault)")
else
  local rewrote = nab.config(cfg)
  check("config read + verify path", rewrote == false,
        string.format("ssid=%s rewrite_returned=%s",
                      tostring(cfg.ssid), tostring(rewrote)))
end

print(string.format("<<HW-ACCEPT %d/%d>>", pass, total))
