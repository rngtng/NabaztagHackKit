-- net.arp against Python-generated fixture frames (rabbit A, peer B).

local arp, link = net.arp, net.link

local MAC_A, MAC_B = H"00095b8f3a01", H"d83add112233"
local IP_A, IP_B = H"c0a8002a", H"c0a80001"

local REQ = H[[aaaa030000000806000108000604000100095b8f3a01c0a8002a000000000000
               c0a80001]]
local REP = H[[aaaa0300000008060001080006040002d83add112233c0a8000100095b8f3a01
               c0a8002a]]

eq(arp.request(MAC_A, IP_A, IP_B), REQ, "arp.request matches fixture")
eq(arp.reply(MAC_B, IP_B, MAC_A, IP_A), REP, "arp.reply matches fixture")

local et, p = link.decap(REP)
eq(et, link.ETH_ARP, "reply ethertype")
local r = arp.parse(p)
eq(r.op, arp.REPLY, "parse op")
eq(r.sha, MAC_B, "parse sha")
eq(r.spa, IP_B, "parse spa")
eq(r.tha, MAC_A, "parse tha")
eq(r.tpa, IP_A, "parse tpa")

eq(arp.parse(H"0001 0800 0604 0001"), nil, "parse short arp")
eq(arp.parse(("\0"):rep(28)), nil, "parse non-ethernet arp")

-- input(): learns the sender; answers requests addressed to us
arp.cache = {}
local _, reqp = link.decap(REQ)
local out = arp.input(arp.parse(reqp), MAC_B, IP_B)
eq(arp.cache[IP_A], MAC_A, "input learns requester")
eq(out, REP, "input answers request for our ip with the fixture reply")
eq(arp.input(arp.parse(reqp), MAC_B, H"c0a80063"), nil,
   "input ignores request for someone else")
arp.cache = {}
eq(arp.input(arp.parse(p), MAC_A, IP_A), nil, "reply input sends nothing")
eq(arp.cache[IP_B], MAC_B, "reply input learns peer")
