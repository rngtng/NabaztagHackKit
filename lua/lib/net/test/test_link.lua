-- net.link: SNAP encap/decap, RFC 1071 checksum, address formatting.

local link = net.link

-- encap produces the canonical LLC/SNAP header (fixture: python fixtures)
eq(link.encap(0x0806, "X"), H"aaaa 0300 0000 0806" .. "X", "link.encap snap+ethertype")

local et, p = link.decap(H"aaaa 0300 0000 0800" .. "PAYLOAD")
eq(et, 0x0800, "link.decap ethertype")
eq(p, "PAYLOAD", "link.decap payload")
eq(link.decap("\1\2\3"), nil, "link.decap short frame")
eq(link.decap(H"ffaa 0300 0000 0800 00"), nil, "link.decap non-snap")

-- RFC 1792 example-style vectors, cross-checked against the Python cksum()
eq(link.checksum(""), 0xFFFF, "checksum empty")
eq(link.checksum(H"0001"), 0xFFFE, "checksum one word")
eq(link.checksum(H"ffff ffff"), 0x0000, "checksum all-ones")
eq(link.checksum("\1"), 0xFEFF, "checksum odd tail pads right")
-- the fixture ping's IP header sums to its checksum field b8ac
local iphdr = H"4500 0024 00b1 4000 4001 0000 c0a8 0001 c0a8 002a"
eq(link.checksum(iphdr), 0xb8ac, "checksum real IP header")
eq(link.checksum(H"4500 0024 00b1 4000 4001 b8ac c0a8 0001 c0a8 002a"), 0,
   "checksum verifies to 0 with field in place")

eq(link.ip("192.168.0.42"), H"c0a8002a", "link.ip dotted quad")
eq(link.ntoa(H"c0a8002a"), "192.168.0.42", "link.ntoa round trip")
ok(not pcall(link.ip, "192.168.0"), "link.ip rejects 3 octets")
ok(not pcall(link.ip, "300.1.1.1"), "link.ip rejects octet > 255")
eq(link.mac2s(H"00095b8f3a01"), "00:09:5b:8f:3a:01", "link.mac2s")
eq(#link.BCAST, 6, "broadcast mac is 6 bytes")
