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

-- Length sweep across every loop boundary (#248). The implementation consumes
-- 8 bytes per string.byte call with a 2-byte tail loop and an odd-byte tail, so
-- lengths 0..24 exercise each path and each transition between them; a
-- block-boundary off-by-one shows up here and nowhere else. Expected values are
-- from the independent Python cksum() (see run.lua's header), NOT from running
-- the Lua - so a rewrite cannot ratify its own bug.
local function pattern(n) -- byte i (1-based) = (i*37+11) % 256; no zero runs
  local t = {}
  for i = 1, n do t[i] = string.char((i * 37 + 11) % 256) end
  return table.concat(t)
end

for _, c in ipairs{
  {0, 0xffff}, {1, 0xcfff}, {2, 0xcfaa}, {3, 0x55aa}, {4, 0x550b},
  {5, 0x910a}, {6, 0x9021}, {7, 0x8221}, {8, 0x81ee}, {9, 0x29ee},
  {10, 0x2971}, {11, 0x8770}, {12, 0x86a9}, {13, 0x9aa8}, {14, 0x9a97},
  {15, 0x6497}, {16, 0x643c}, {17, 0xe43b}, {18, 0xe396}, {19, 0x1996},
  {20, 0x18a7}, {21, 0x04a7}, {22, 0x046e}, {23, 0xa66d}, {24, 0xa5ea},
  {1199, 0xcb90}, {1200, 0xcb15},
} do
  eq(link.checksum(pattern(c[1])), c[2], "checksum length " .. c[1])
end

-- Degenerate content at a block boundary: all-zero must not be confused with
-- "no data", all-ones must still fold the carry.
eq(link.checksum(("\0"):rep(16)), 0xFFFF, "checksum 16 zero bytes")
eq(link.checksum(("\255"):rep(16)), 0x0000, "checksum 16 all-ones bytes")
eq(link.checksum(("\255"):rep(9)), 0x00FF, "checksum 9 all-ones bytes")

eq(link.ip("192.168.0.42"), H"c0a8002a", "link.ip dotted quad")
eq(link.ntoa(H"c0a8002a"), "192.168.0.42", "link.ntoa round trip")
ok(not pcall(link.ip, "192.168.0"), "link.ip rejects 3 octets")
ok(not pcall(link.ip, "300.1.1.1"), "link.ip rejects octet > 255")
eq(link.mac2s(H"00095b8f3a01"), "00:09:5b:8f:3a:01", "link.mac2s")
eq(#link.BCAST, 6, "broadcast mac is 6 bytes")
