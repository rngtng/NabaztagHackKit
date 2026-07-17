-- net.ipv4 + ICMP echo against Python-generated fixtures.

local ipv4, link = net.ipv4, net.link

local IP_A, IP_B = H"c0a8002a", H"c0a80001"

local PING = H[[4500002400b140004001b8acc0a80001c0a8002a080065670102000161626364
                65666768]]

local pkt = ipv4.parse(PING)
eq(pkt.src, IP_B, "parse src")
eq(pkt.dst, IP_A, "parse dst")
eq(pkt.proto, ipv4.ICMP, "parse proto")
eq(pkt.ttl, 64, "parse ttl")
eq(pkt.id, 0x00b1, "parse id")
eq(pkt.payload, H"0800 6567 0102 0001" .. "abcdefgh", "parse payload")

-- corruption and edge cases must be rejected with the specific reason
local function err(p) local _, e = ipv4.parse(p) return e end
eq(err(PING:sub(1, 19)), "short ipv4", "short packet")
eq(err("\80" .. PING:sub(2)), "not ipv4", "version 4 required")
eq(err("\68" .. PING:sub(2)), "bad ihl", "ihl below 20")
eq(err(PING:sub(1, 9) .. "\0" .. PING:sub(11)), "bad checksum", "bit flip caught")
eq(err(H"4500 0024 00b1 5fff 4001 98ad c0a8 0001 c0a8 002a" .. PING:sub(21)),
   "fragment", "fragments dropped")
-- total length beyond the buffer must not be trusted
eq(err(H"4500 ffff 00b1 4000 4001 b8d0 c0a8 0001 c0a8 002a"), "bad length",
   "lying total length")

-- build round-trips through parse and pads nothing
local b = ipv4.build{src = IP_A, dst = IP_B, proto = 17, payload = "hi"}
eq(#b, 22, "build length 20+2")
local pb = ipv4.parse(b)
eq(pb.src, IP_A, "build src")
eq(pb.proto, 17, "build proto")
eq(pb.payload, "hi", "build payload")
eq(b:byte(7) >> 5, 2, "build sets DF")
ok(ipv4.build{src = IP_A, dst = IP_B, proto = 17, payload = ""}:byte(6) ~=
   b:byte(6), "id increments per packet")

-- ICMP echo: reply mirrors id/seq/data with the independently computed cksum
local reply = ipv4.icmp_input(pkt)
local rp = ipv4.parse(reply)
eq(rp.src, IP_A, "echo reply from us")
eq(rp.dst, IP_B, "echo reply to pinger")
eq(rp.payload, H"0000 6d67 0102 0001" .. "abcdefgh",
   "echo reply icmp bytes (fixture cksum 6d67)")
eq(ipv4.icmp_input{payload = H"0000 6d67 0102 0001", src = IP_B, dst = IP_A},
   nil, "echo replies are not answered")
