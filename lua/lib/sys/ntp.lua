-- sys.ntp - SNTP v4 client packets (RFC 4330): a 48-byte request out, and the
-- transmit timestamp at offset 40 of the reply back (#259).
--
-- Pure build/parse over byte strings, the same pull shape as net.dhcp - the
-- caller owns the socket (net.iface's :ntp drives it) and the clock
-- (sys.time stores the result). Seconds are what the rabbit needs; the
-- fraction is reported in milliseconds and otherwise ignored, since neither
-- the round trip nor the 1 ms tick is worth more precision than that.
--
-- Device stdlib is base + table + string only; this file is one luac chunk
-- that extends the global `sys` table (there is no require on the rabbit).

sys = sys or {}
local ntp = {}
sys.ntp = ntp

ntp.PORT = 123

-- NTP counts seconds from 1900-01-01, Unix from 1970-01-01 - 2208988800 s
-- apart. Under LUA_32BITS this literal *is* the negative -2085978496, and the
-- subtraction below is deliberately mod-2^32: that is also why the era-1
-- rollover on 2036-02-07T06:28:16Z needs no special case, because the wrap in
-- the server's counter and the wrap in our arithmetic cancel exactly. The real
-- cliff is 2038-01-19, where the Unix seconds themselves stop fitting a 32-bit
-- signed lua_Integer.
local EPOCH_1900 = 0x83AA7E80

-- LI 0 (no warning), VN 4, mode 3 (client)
local HEADER = "\35"

-- `cookie` (optional, exactly 8 bytes) is planted in the transmit timestamp;
-- a server echoes it back in the originate field, which is what parse()
-- matches to drop a stale or spoofed reply. -> 48-byte request | nil, err
function ntp.build(cookie)
  if cookie ~= nil and (type(cookie) ~= "string" or #cookie ~= 8) then
    return nil, "bad cookie"
  end
  return HEADER .. ("\0"):rep(39) .. (cookie or ("\0"):rep(8))
end

-- reply datagram -> {epoch=,ms=,stratum=,li=} | nil, err. `cookie`, when
-- given, must match the originate timestamp.
function ntp.parse(d, cookie)
  if type(d) ~= "string" or #d < 48 then return nil, "short ntp" end
  local b = d:byte(1)
  local li, vn, mode = b >> 6, (b >> 3) & 7, b & 7
  if mode ~= 4 and mode ~= 5 then return nil, "not a server reply" end
  if vn < 3 or vn > 4 then return nil, "bad version" end
  if li == 3 then return nil, "unsynchronised" end -- alarm: clock not set
  local stratum = d:byte(2)
  if stratum == 0 then return nil, "kiss of death" end -- rate limit / deny
  if stratum > 15 then return nil, "bad stratum" end
  if cookie and d:sub(25, 32) ~= cookie then return nil, "wrong originate" end
  local secs, frac = string.unpack(">I4I4", d, 41)
  -- An all-zero transmit timestamp is an unset clock. It also collides with
  -- the one second at the era-1 rollover instant; losing that second (the
  -- caller retries) beats trusting every server that never filled the field.
  if secs == 0 then return nil, "no transmit time" end
  return {epoch = secs - EPOCH_1900, ms = (frac >> 16) * 1000 // 65536,
          stratum = stratum, li = li}
end
