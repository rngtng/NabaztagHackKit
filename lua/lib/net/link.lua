-- net.link - the 802.3 payload boundary (#217).
--
-- nab.wifi_send/recv (#216) carry the bytes after the 802.11 header, so the
-- first thing Lua sees on the wire is the LLC/SNAP header; this module owns
-- that plus the RFC 1071 checksum and address formatting shared by every
-- layer above. MACs are 6-byte and IPv4 addresses 4-byte binary strings end
-- to end - they only become text for printing.
--
-- Device stdlib is base + table + string only; each module is one luac chunk
-- that extends the global `net` table (there is no require on the rabbit).

net = net or {}
local link = {}
net.link = link

link.ETH_IP = 0x0800
link.ETH_ARP = 0x0806
link.BCAST = "\255\255\255\255\255\255"

local SNAP = "\170\170\3\0\0\0" -- LLC DSAP/SSAP AA AA, UI, OUI 00-00-00

function link.encap(ethertype, payload)
  return SNAP .. string.pack(">I2", ethertype) .. payload
end

-- frame payload (from nab.wifi_recv) -> ethertype, inner packet | nil, err
function link.decap(frame)
  if #frame < 8 then return nil, "short frame" end
  if frame:sub(1, 6) ~= SNAP then return nil, "not LLC/SNAP" end
  return string.unpack(">I2", frame, 7), frame:sub(9)
end

-- RFC 1071 ones-complement checksum over big-endian 16-bit words (odd tail
-- zero-padded). Verification: checksum over data that includes a valid
-- checksum field returns 0.
--
-- This is the hot path of the whole stack - every byte in and out is summed
-- here - so it consumes 8 bytes per string.byte call rather than one word per
-- string.unpack. unpack re-parses its format string on every call; byte does
-- not, and returns 8 values for the same call overhead. Measured on the host
-- lua (1200-byte packet): 42.8 -> 14.5 us, 2.95x (#248).
--
-- Overflow: each 8-byte block adds at most 4 * 0xFFFF, so a 1500-byte frame
-- peaks near 49e6 and even a 64 KB input stays under 2.2e9 - within the 32-bit
-- signed range LUA_32BITS gives us. The fold below then reduces to 16 bits.
function link.checksum(s)
  local byte = string.byte
  local n = #s
  local sum = 0
  local i = 1
  while i + 7 <= n do                     -- whole 8-byte blocks
    local a, b, c, d, e, f, g, h = byte(s, i, i + 7)
    sum = sum + a * 256 + b + c * 256 + d + e * 256 + f + g * 256 + h
    i = i + 8
  end
  while i + 1 <= n do                     -- remaining whole words
    local a, b = byte(s, i, i + 1)
    sum = sum + a * 256 + b
    i = i + 2
  end
  if i == n then sum = sum + (byte(s, n) << 8) end   -- odd tail, padded right
  while sum > 0xFFFF do sum = (sum & 0xFFFF) + (sum >> 16) end
  return ~sum & 0xFFFF
end

-- "192.168.0.1" -> 4-byte string (string.char rejects out-of-range octets)
function link.ip(s)
  local a, b, c, d = s:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
  assert(a, "bad IPv4 address")
  return string.char(a, b, c, d)
end

function link.ntoa(ip)
  return ("%d.%d.%d.%d"):format(ip:byte(1, 4))
end

function link.mac2s(m)
  return (m:gsub(".", function(c) return ("%02x:"):format(c:byte()) end)
          :sub(1, -2))
end
