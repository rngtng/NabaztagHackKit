-- net.udp - UDP datagram build/parse with the IPv4 pseudo-header checksum
-- (#217). Checksums are always computed on TX (0 is replaced by 0xFFFF per
-- RFC 768) and verified on RX unless the sender opted out with 0.

net = net or {}
local udp = {}
net.udp = udp
local link, ipv4 = net.link, net.ipv4

local function pseudo(src, dst, len)
  return src .. dst .. string.pack(">BBI2", 0, ipv4.UDP, len)
end

-- 4-byte src/dst IPs (for the pseudo-header) -> UDP datagram (pre-IP)
function udp.build(src_ip, sport, dst_ip, dport, payload)
  local len = 8 + #payload
  local ck = link.checksum(pseudo(src_ip, dst_ip, len)
                           .. string.pack(">I2I2I2I2", sport, dport, len, 0)
                           .. payload)
  if ck == 0 then ck = 0xFFFF end
  return string.pack(">I2I2I2I2", sport, dport, len, ck) .. payload
end

-- parsed IPv4 packet (proto UDP) -> {sport=,dport=,payload=} | nil, err
function udp.parse(pkt)
  local p = pkt.payload
  if #p < 8 then return nil, "short udp" end
  local sport, dport, len, ck = string.unpack(">I2I2I2I2", p)
  if len < 8 or len > #p then return nil, "bad length" end
  if ck ~= 0
     and link.checksum(pseudo(pkt.src, pkt.dst, len) .. p:sub(1, len)) ~= 0 then
    return nil, "bad checksum"
  end
  return {sport = sport, dport = dport, payload = p:sub(9, len)}
end
