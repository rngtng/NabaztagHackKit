-- net.arp - ARP over 802.3/SNAP (#217): request/reply build + parse and a
-- learned ip->mac cache. Answer/query policy lives in net.iface.

net = net or {}
local arp = {}
net.arp = arp
local link = net.link

arp.REQUEST, arp.REPLY = 1, 2

arp.cache = {} -- [4-byte ip] = 6-byte mac, learned from replies and requests

-- sha/tha 6-byte MACs, spa/tpa 4-byte IPs -> ready-to-send frame payload
function arp.build(op, sha, spa, tha, tpa)
  return link.encap(link.ETH_ARP,
    string.pack(">I2I2BBI2", 1, link.ETH_IP, 6, 4, op)
    .. sha .. spa .. tha .. tpa)
end

function arp.request(my_mac, my_ip, ip)
  return arp.build(arp.REQUEST, my_mac, my_ip, "\0\0\0\0\0\0", ip)
end

function arp.reply(my_mac, my_ip, tha, tpa)
  return arp.build(arp.REPLY, my_mac, my_ip, tha, tpa)
end

-- ARP packet (post-decap) -> {op=,sha=,spa=,tha=,tpa=} | nil, err
function arp.parse(p)
  if #p < 28 then return nil, "short arp" end
  local htype, ptype, hlen, plen, op, pos = string.unpack(">I2I2BBI2", p)
  if htype ~= 1 or ptype ~= link.ETH_IP or hlen ~= 6 or plen ~= 4 then
    return nil, "not ethernet/ipv4 arp"
  end
  return {op = op, sha = p:sub(pos, pos + 5), spa = p:sub(pos + 6, pos + 9),
          tha = p:sub(pos + 10, pos + 15), tpa = p:sub(pos + 16, pos + 19)}
end

-- Learn the sender, and produce the reply frame if the request asks for us.
-- p is a parsed packet; returns a frame to send | nil.
function arp.input(p, my_mac, my_ip)
  arp.cache[p.spa] = p.sha
  if p.op == arp.REQUEST and p.tpa == my_ip then
    return arp.reply(my_mac, my_ip, p.sha, p.spa)
  end
end
