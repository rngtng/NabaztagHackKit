-- net.ipv4 - IPv4 header build/parse + ICMP echo responder (#217).
--
-- Fragments are dropped (we never send near-MTU packets and reassembly is
-- not worth its buffer on this device). The ICMP echo responder rides along
-- because ping is the first thing to point at new hardware.

net = net or {}
local ipv4 = {}
net.ipv4 = ipv4
local link = net.link

ipv4.ICMP, ipv4.TCP, ipv4.UDP = 1, 6, 17

local id = 0 -- rolling IP identification

-- {src=,dst=,proto=,payload=[,ttl=,tos=]} -> IPv4 packet (pre-SNAP)
function ipv4.build(o)
  id = (id + 1) & 0xFFFF
  local hdr = string.pack(">BBI2I2I2BBI2", 0x45, o.tos or 0,
                          20 + #o.payload, id, 0x4000, -- DF, no fragments
                          o.ttl or 64, o.proto, 0) .. o.src .. o.dst
  return hdr:sub(1, 10) .. string.pack(">I2", link.checksum(hdr))
         .. hdr:sub(13) .. o.payload
end

-- packet -> {src=,dst=,proto=,ttl=,id=,payload=} | nil, err
function ipv4.parse(p)
  if #p < 20 then return nil, "short ipv4" end
  local vi = p:byte(1)
  if vi >> 4 ~= 4 then return nil, "not ipv4" end
  local ihl = (vi & 0xF) * 4
  if ihl < 20 or #p < ihl then return nil, "bad ihl" end
  if link.checksum(p:sub(1, ihl)) ~= 0 then return nil, "bad checksum" end
  local total = string.unpack(">I2", p, 3)
  if total < ihl or total > #p then return nil, "bad length" end
  if string.unpack(">I2", p, 7) & 0x3FFF ~= 0 then return nil, "fragment" end
  return {ttl = p:byte(9), proto = p:byte(10), id = string.unpack(">I2", p, 5),
          src = p:sub(13, 16), dst = p:sub(17, 20),
          payload = p:sub(ihl + 1, total)}
end

-- Echo request (parsed packet, proto ICMP) -> echo-reply IPv4 packet | nil
function ipv4.icmp_input(pkt)
  local p = pkt.payload
  if #p < 8 or p:byte(1) ~= 8 or p:byte(2) ~= 0 then return nil end
  local rest = p:sub(5)
  return ipv4.build{src = pkt.dst, dst = pkt.src, proto = ipv4.ICMP,
                    payload = "\0\0"
                      .. string.pack(">I2", link.checksum("\0\0\0\0" .. rest))
                      .. rest}
end
