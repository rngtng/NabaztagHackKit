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
-- checksum field returns 0. Sums stay well under 2^31, so 32-bit ints are safe.
function link.checksum(s)
  local sum = 0
  local n = #s
  for i = 1, n - 1, 2 do
    sum = sum + string.unpack(">I2", s, i)
  end
  if n % 2 == 1 then sum = sum + (s:byte(n) << 8) end
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
