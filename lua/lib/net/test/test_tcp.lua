-- net.tcp: fixture handshake/data/FIN from a real-shaped server, then a full
-- loopback client<->listener session with loss, duplication and teardown.

local tcp, ipv4, link = net.tcp, net.ipv4, net.link

local IP_A, IP_B = H"c0a8002a", H"c0a80001"
local BODY = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello"

local SYNACK = H[[4500002c00b340004006b89dc0a80001c0a8002a00509c4100012fd1000003e9
                  60122000264e0000020405b4]]
local DATA = H[[4500005300b440004006b875c0a80001c0a8002a00509c4100012fd2000003f9
                501820004af30000485454502f312e3020323030204f4b0d0a436f6e74656e74
                2d4c656e6774683a20350d0a0d0a68656c6c6f]]
local FIN = H[[4500002800b540004006b89fc0a80001c0a8002a00509c4100012ffd000003f9
               501120003dcf0000]]

local t = 0
local function clk() return t end

-- build a segment from peer B:80, for the parts fixtures don't cover
local function from_b(seq, ack, flags, payload)
  payload = payload or ""
  local hdr = string.pack(">I2I2I4I4BBI2I2I2", 80, 40001, seq, ack, 5 << 4,
                          flags, 8192, 0, 0)
  local ck = link.checksum(IP_B .. IP_A .. string.pack(">BBI2", 0, ipv4.TCP,
                           #hdr + #payload) .. hdr .. payload)
  return ipv4.parse(ipv4.build{src = IP_B, dst = IP_A, proto = ipv4.TCP,
    payload = hdr:sub(1, 16) .. string.pack(">I2", ck) .. hdr:sub(19)
              .. payload})
end

-- segment fields of the first packet in an output array
local function seg1(out)
  return tcp.parse(ipv4.parse(out[1]))
end

-- fixture-driven client: connect -> GET -> body -> FIN -> close ------------

local c = tcp.client{src = IP_A, dst = IP_B, sport = 40001, dport = 80,
                     iss = 1000, clock = clk}
local syn = c:connect()
eq(#syn, 1, "connect emits one segment")
local ss = seg1(syn)
eq(ss.sport, 40001, "syn sport")
eq(ss.dport, 80, "syn dport")
eq(ss.seq, 1000, "syn seq = iss")
eq(ss.flags, tcp.SYN, "syn flags")
eq(ss.mss, 1460, "syn advertises mss 1460")
eq(c.state, "syn-sent", "state syn-sent")

t = 2999
eq(#c:poll(), 0, "no retransmit before RTO")
t = 3000
local rs = c:poll()
eq(#rs, 1, "syn retransmitted at RTO")
eq(seg1(rs).seq, 1000, "retransmit keeps seq")

local est = c:input(ipv4.parse(SYNACK))
eq(c.state, "established", "synack establishes")
eq(c.mss, 1200, "peer mss 1460 capped at our frame budget")
local ak = seg1(est)
eq(ak.flags, tcp.ACK, "handshake ack flags")
eq(ak.seq, 1001, "handshake ack seq")
eq(ak.ack, 77778, "handshake ack number")

local dat = c:send("GET / HTTP/1.0\r\n")
local ds = seg1(dat)
eq(ds.seq, 1001, "data seq")
eq(ds.flags, tcp.PSH | tcp.ACK, "data flags")
eq(ds.payload, "GET / HTTP/1.0\r\n", "data payload")

local resp = c:input(ipv4.parse(DATA))
eq(c:read(), BODY, "server body received in order")
eq(seg1(resp).ack, 77821, "their 43 bytes acked")
eq(c.rtx, nil, "our data segment cleared by their ack")

-- the same data segment again: dup-ACKed, not appended
local dup = c:input(ipv4.parse(DATA))
eq(seg1(dup).ack, 77821, "duplicate gets dup-ack")
eq(c:read(), "", "duplicate not appended")

local fr = c:input(ipv4.parse(FIN))
eq(c.state, "close-wait", "their fin -> close-wait")
eq(seg1(fr).ack, 77822, "fin consumed one seq")

local cl = c:close()
eq(c.state, "last-ack", "our fin -> last-ack")
local fs = seg1(cl)
eq(fs.flags, tcp.FIN | tcp.ACK, "fin flags")
eq(fs.seq, 1017, "fin seq after 16 data bytes")
c:input(from_b(77822, 1018, tcp.ACK))
eq(c.state, "closed", "acked fin closes")
eq(c.err, nil, "clean close has no error")

-- reset and give-up paths --------------------------------------------------

t = 0
local r = tcp.client{src = IP_A, dst = IP_B, sport = 40001, dport = 80,
                     iss = 1000, clock = clk}
r:connect()
r:input(ipv4.parse(SYNACK))
r:input(from_b(77778, 1001, tcp.RST))
eq(r.state, "closed", "rst closes")
eq(r.err, "reset", "rst reported")

local g = tcp.client{src = IP_A, dst = IP_B, sport = 40001, dport = 80,
                     clock = clk}
g:connect()
for _ = 1, 9 do
  t = t + 3000
  g:poll()
end
eq(g.state, "closed", "unanswered syn gives up")
eq(g.err, "timeout", "give-up reported")

-- loopback: our client against our listener --------------------------------

t = 0
local a = tcp.client{src = IP_A, dst = IP_B, sport = 5000, dport = 80,
                     iss = 100, clock = clk}
local b = tcp.listen{src = IP_B, port = 80, iss = 900, clock = clk}

local function xfer(to, pkts)
  local outs = {}
  for _, p in ipairs(pkts) do
    for _, q in ipairs(to:input(ipv4.parse(p))) do outs[#outs + 1] = q end
  end
  return outs
end

local function settle(x, y, pkts)
  local from, to, n = x, y, 0
  while #pkts > 0 and n < 50 do
    pkts = xfer(to, pkts)
    from, to = to, from
    n = n + 1
  end
  eq(#pkts, 0, "exchange settles")
end

settle(a, b, a:connect())
eq(a.state, "established", "client established")
eq(b.state, "established", "listener established")
eq(b.dport, 5000, "listener learned peer port")
eq(b.mss, 1200, "listener capped mss")

settle(a, b, a:send("GET /config HTTP/1.0\r\n\r\n"))
eq(b:read(), "GET /config HTTP/1.0\r\n\r\n", "request crosses")

-- 3000 bytes forces mss chunking (1200+1200+600), stop-and-wait acked
settle(b, a, b:send(("s"):rep(3000)))
eq(a:read(), ("s"):rep(3000), "3000 bytes cross in mss chunks")

-- lost segment: nothing delivered, retransmit after RTO carries it
local lost = a:send("Q")
eq(#lost, 1, "segment sent into the void")
t = t + 3000
settle(a, b, a:poll())
eq(b:read(), "Q", "retransmit delivers exactly once")

settle(a, b, a:close())
eq(a.state, "fin-wait-2", "client half-closed")
eq(b.state, "close-wait", "listener sees the fin")
settle(b, a, b:close())
eq(b.state, "closed", "listener closed")
eq(a.state, "closed", "client closed")
eq(a.err, nil, "loopback teardown clean")
