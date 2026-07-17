-- net.udp against the Python fixture datagram (B:5353 -> A:8888).

local udp, ipv4 = net.udp, net.ipv4

local IP_A, IP_B = H"c0a8002a", H"c0a80001"
local DGRAM = H"14e9 22b8 0014 c47e" .. "hello rabbit"

eq(udp.build(IP_B, 5353, IP_A, 8888, "hello rabbit"), DGRAM,
   "build matches fixture incl. checksum c47e")

local pkt = {src = IP_B, dst = IP_A, payload = DGRAM}
local d = udp.parse(pkt)
eq(d.sport, 5353, "parse sport")
eq(d.dport, 8888, "parse dport")
eq(d.payload, "hello rabbit", "parse payload")

local function err(payload, src)
  local _, e = udp.parse{src = src or IP_B, dst = IP_A, payload = payload}
  return e
end
eq(err(DGRAM:sub(1, 7)), "short udp", "short datagram")
eq(err(DGRAM:sub(1, 5) .. "\3" .. DGRAM:sub(7)), "bad length",
   "length below 8")
eq(err(DGRAM, H"c0a80002"), "bad checksum",
   "pseudo-header mismatch caught (spoofed src ip)")
eq(err(DGRAM:sub(1, 8) .. "Hello rabbit"), "bad checksum", "payload bit flip")
-- checksum 0 = sender opted out: must parse
local nock = DGRAM:sub(1, 6) .. "\0\0" .. "hello rabbit"
eq(udp.parse{src = IP_B, dst = IP_A, payload = nock}.payload, "hello rabbit",
   "zero checksum accepted")
-- trailing IP padding beyond the UDP length is sliced off
eq(udp.parse{src = IP_B, dst = IP_A, payload = nock .. "\0\0"}.payload,
   "hello rabbit", "ip padding ignored")

-- a build over the ipv4 layer round-trips
local b = ipv4.parse(ipv4.build{src = IP_A, dst = IP_B, proto = ipv4.UDP,
                                payload = udp.build(IP_A, 68, IP_B, 67, "x")})
eq(udp.parse(b).dport, 67, "udp over ipv4 round trip")
