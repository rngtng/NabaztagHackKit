-- net.dhcp: fixture OFFER/ACK through the client, server replies, and a full
-- client<->server exchange.

local dhcp, link, ipv4, udp = net.dhcp, net.link, net.ipv4, net.udp

local MAC_A = H"00095b8f3a01"
local IP_A, IP_B = H"c0a8002a", H"c0a80001"
local XID = 0x3903F326

local LEASE_OPTS = H"3604c0a80001 33040001 5180 0104ffffff00 0304c0a80001 060408080808"
local OFFER = H[[020106003903f3260000800000000000c0a8002ac0a800010000000000095b8f
  3a01000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  000000000000000000000000638253633501023604c0a8000133040001518001
  04ffffff000304c0a80001060408080808ff]]
local ACK = OFFER:sub(1, 242) .. "\5" .. OFFER:sub(244)

-- parse: every fixture field, positively
local r = dhcp.parse(OFFER)
eq(r.op, 2, "parse op")
eq(r.xid, XID, "parse xid")
eq(r.yiaddr, IP_A, "parse yiaddr")
eq(r.siaddr, IP_B, "parse siaddr")
eq(r.mac, MAC_A, "parse chaddr mac")
eq(r.msgtype, dhcp.OFFER, "parse msgtype option 53")
eq(r.opts[1], H"ffffff00", "parse mask option")
eq(r.opts[51], H"00015180", "parse lease-time option")
eq(dhcp.parse(("\0"):rep(239)), nil, "short dhcp rejected")
eq(dhcp.parse(("\0"):rep(240)), nil, "bad magic rejected")
local _, e = dhcp.parse(OFFER:sub(1, 240) .. "\53\200")
eq(e, "bad option", "truncated option rejected")

-- unwrap a client/server frame down to the DHCP payload, asserting the
-- addressing on the way
local function unwrap(frame, sport, dport)
  local et, p = link.decap(frame)
  eq(et, link.ETH_IP, "frame is ip")
  local pkt = ipv4.parse(p)
  eq(pkt.dst, H"ffffffff", "frame is broadcast")
  local d = udp.parse(pkt)
  eq(d.sport, sport, "frame sport")
  eq(d.dport, dport, "frame dport")
  return d.payload
end

-- client: DISCOVER -> (fixture OFFER) -> REQUEST -> (fixture ACK) -> lease
local c = dhcp.client(MAC_A, XID)
local disc = dhcp.parse(unwrap(c:discover(), 68, 67))
eq(disc.op, 1, "discover op")
eq(disc.xid, XID, "discover xid")
eq(disc.mac, MAC_A, "discover chaddr")
eq(disc.msgtype, dhcp.DISCOVER, "discover msgtype")
eq(c.state, "selecting", "state selecting")

local reqf = c:input(OFFER)
local req = dhcp.parse(unwrap(reqf, 68, 67))
eq(req.msgtype, dhcp.REQUEST, "offer answered with request")
eq(req.opts[50], IP_A, "request asks for the offered ip")
eq(req.opts[54], IP_B, "request names the offering server")
eq(c.state, "requesting", "state requesting")
eq(c:retransmit(), reqf, "retransmit re-yields the in-flight frame")

local f2, lease = c:input(ACK)
eq(f2, nil, "ack sends nothing")
eq(c.state, "bound", "state bound")
eq(lease.ip, IP_A, "lease ip")
eq(lease.mask, H"ffffff00", "lease mask")
eq(lease.router, IP_B, "lease router")
eq(lease.dns, H"08080808", "lease dns")
eq(lease.server, IP_B, "lease server id")
eq(lease.ttl, 86400, "lease ttl seconds")

-- wrong xid / stray messages are ignored
local c2 = dhcp.client(MAC_A, 1)
c2:discover()
eq(c2:input(OFFER), nil, "foreign xid ignored")
eq(c2.state, "selecting", "state unchanged by foreign xid")

-- server: single fixed lease, NAK on a stale address
local s = dhcp.server{ip = IP_B, client_ip = IP_A}
local off = dhcp.parse(unwrap(s:input(dhcp.build{
  op = 1, xid = 7, mac = MAC_A, msgtype = dhcp.DISCOVER}), 67, 68))
eq(off.msgtype, dhcp.OFFER, "server offers on discover")
eq(off.yiaddr, IP_A, "server offers the fixed lease")
eq(off.opts[54], IP_B, "server id option")
eq(off.opts[6], IP_B, "dns points at the portal (#218)")

local ack = dhcp.parse(unwrap(s:input(dhcp.build{
  op = 1, xid = 7, mac = MAC_A, msgtype = dhcp.REQUEST,
  opts = {{50, IP_A}}}), 67, 68))
eq(ack.msgtype, dhcp.ACK, "server acks the right request")
eq(ack.yiaddr, IP_A, "ack yiaddr")

local nak = dhcp.parse(unwrap(s:input(dhcp.build{
  op = 1, xid = 7, mac = MAC_A, msgtype = dhcp.REQUEST,
  opts = {{50, H"0a000005"}}}), 67, 68))
eq(nak.msgtype, dhcp.NAK, "server naks a stale address")
eq(s:input(dhcp.build{op = 2, xid = 7, mac = MAC_A, msgtype = dhcp.OFFER}),
   nil, "server ignores replies")

-- full loop: our client against our server ends bound to the fixed lease
local c3 = dhcp.client(MAC_A, 99)
local s3 = dhcp.server{ip = IP_B, client_ip = IP_A}
local r1 = s3:input(unwrap(c3:discover(), 68, 67))
local r2 = c3:input(unwrap(r1, 67, 68))
local _, l3 = c3:input(unwrap(s3:input(unwrap(r2, 68, 67)), 67, 68))
eq(l3.ip, IP_A, "client<->server loop binds the lease")
eq(c3.state, "bound", "loop ends bound")
