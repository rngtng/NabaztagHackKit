-- net.iface over a scripted fake driver: demux, learning, ARP-on-demand,
-- and the three blocking flows (dhcp join, http_get, serve).

local iface, link, arp, ipv4, udp, dhcp, tcp, http =
  net.iface, net.link, net.arp, net.ipv4, net.udp, net.dhcp, net.tcp, net.http

local MAC_A, MAC_B = H"00095b8f3a01", H"d83add112233"
local IP_A, IP_B = H"c0a8002a", H"c0a80001"
local PING = H[[4500002400b140004001b8acc0a80001c0a8002a080065670102000161626364
                65666768]]

local t, sent, rxq, hook = 0, {}, {}, nil
local function timefn() return t end
local drv = {
  mac = MAC_A, time = timefn,
  send = function(mac, f)
    sent[#sent + 1] = {mac = mac, f = f}
    if hook then hook(mac, f) end
  end,
  recv = function(ms)
    t = t + (ms or 0) -- waiting advances the fake clock
    local e = table.remove(rxq, 1)
    if e then return e[1], e[2] end
  end,
}
local function push(frame) rxq[#rxq + 1] = {MAC_B, frame} end
local function pushpkt(pkt) push(link.encap(link.ETH_IP, pkt)) end
local function lastf() return sent[#sent].mac, sent[#sent].f end

-- icmp echo + passive mac learning ----------------------------------------

arp.cache = {}
local i = iface.new(drv)
i.ip = IP_A
push(link.encap(link.ETH_IP, PING))
i:poll(0)
local mac, f = lastf()
eq(mac, MAC_B, "echo reply goes straight back to the frame's mac")
local et, p = link.decap(f)
eq(et, link.ETH_IP, "echo reply is ip")
eq(ipv4.parse(p).payload:sub(1, 4), H"00006d67", "echo reply icmp fixture bytes")
eq(arp.cache[IP_B], MAC_B, "sender mac learned passively")

-- arp request answered -----------------------------------------------------

push(arp.request(MAC_B, IP_B, IP_A))
i:poll(0)
mac, f = lastf()
eq(mac, MAC_B, "arp reply unicast")
local _, ap = link.decap(f)
eq(arp.parse(ap).op, arp.REPLY, "arp reply op")
eq(arp.parse(ap).spa, IP_A, "arp reply claims our ip")

-- ipsend: unknown mac -> arp request; off-subnet -> via router --------------

arp.cache = {}
local n0 = #sent
i:ipsend(IP_B, "IGNORED")
mac, f = lastf()
eq(mac, link.BCAST, "unresolved send falls back to arp request")
eq(arp.parse(select(2, link.decap(f))).tpa, IP_B, "arp asks for the hop")
i.mask, i.router = link.ip("255.255.255.0"), IP_B
arp.cache[IP_B] = MAC_B
i:ipsend(H"08080808", "PKT")
mac, f = lastf()
eq(mac, MAC_B, "off-subnet send goes to the router's mac")
eq(f, link.encap(link.ETH_IP, "PKT"), "payload snap-wrapped")
eq(#sent, n0 + 2, "one frame per ipsend")

-- dhcp join against our own server ------------------------------------------

arp.cache = {}
t, sent, rxq = 0, {}, {}
local i2 = iface.new(drv)
local srv = dhcp.server{ip = IP_B, client_ip = IP_A}
hook = function(mac2, f2)
  if mac2 ~= link.BCAST then return end
  local et2, p2 = link.decap(f2)
  if et2 ~= link.ETH_IP then return end
  local pkt = ipv4.parse(p2)
  if not pkt or pkt.proto ~= ipv4.UDP then return end
  local d = udp.parse(pkt)
  if d and d.dport == 67 then
    local r = srv:input(d.payload)
    if r then push(r) end
  end
end
local lease, lerr = i2:dhcp(15000)
eq(lerr, nil, "dhcp join succeeds")
eq(lease.ip, IP_A, "lease ip")
eq(i2.ip, IP_A, "iface adopts the lease ip")
eq(i2.router, IP_B, "iface adopts the router")
eq(i2.udp_ports[68], nil, "dhcp port handler unregistered")

-- http_get against a scripted peer (arp answered, tcp served) ---------------

t, sent, rxq = 0, {}, {}
local b = tcp.listen{src = IP_B, port = 80, iss = 900, clock = timefn}
local q = http.request()
local responded
hook = function(_, f3)
  local et3, p3 = link.decap(f3)
  if et3 == link.ETH_ARP then
    local a3 = arp.parse(p3)
    if a3 and a3.op == arp.REQUEST then
      push(arp.reply(MAC_B, IP_B, MAC_A, IP_A))
    end
    return
  end
  local pkt = ipv4.parse(p3)
  if not pkt or pkt.proto ~= ipv4.TCP then return end
  for _, o in ipairs(b:input(pkt)) do pushpkt(o) end
  local d3 = b:read()
  if d3 ~= "" then q:feed(d3) end
  if q.done and not responded then
    responded = true
    for _, o in ipairs(b:send(http.response_build("200 OK", "LCDATA"))) do
      pushpkt(o)
    end
    for _, o in ipairs(b:close()) do pushpkt(o) end
  end
end
arp.cache = {}
local status, body = i2:http_get(IP_B, "srv", "/boot/app.lc", 15000)
eq(status, 200, "http_get status")
eq(body, "LCDATA", "http_get body")
eq(q.method, "GET", "peer saw the method")
eq(q.path, "/boot/app.lc", "peer saw the path")
eq(q.headers["host"], "srv", "peer saw the host header")

-- serve: scripted phone fetches the config page and stops the loop ----------

t, sent, rxq = 0, {}, {}
local a = tcp.client{src = IP_B, dst = IP_A, sport = 52000, dport = 80,
                     iss = 5, clock = timefn}
local resp = http.response()
local sentreq, closed
hook = function(_, f4)
  local et4, p4 = link.decap(f4)
  if et4 ~= link.ETH_IP then return end
  local pkt = ipv4.parse(p4)
  if not pkt or pkt.proto ~= ipv4.TCP then return end
  for _, o in ipairs(a:input(pkt)) do pushpkt(o) end
  if a.state == "established" and not sentreq then
    sentreq = true
    for _, o in ipairs(a:send("GET /cfg?ssid=My%20AP HTTP/1.0\r\n\r\n")) do
      pushpkt(o)
    end
  end
  local d4 = a:read()
  if d4 ~= "" then resp:feed(d4) end
  if a.state == "close-wait" and not closed then
    closed = true
    resp:eof()
    for _, o in ipairs(a:close()) do pushpkt(o) end
  end
end
for _, o in ipairs(a:connect()) do pushpkt(o) end
local got
i2:serve(80, function(rq)
  got = rq
  return "<form>ok</form>", "200 OK", true
end)
eq(got.path, "/cfg", "server handler saw the path")
eq(got.query.ssid, "My AP", "server handler got the decoded query")
eq(resp.status, 200, "phone got the status")
eq(resp.body, "<form>ok</form>", "phone got the page")
eq(a.state, "closed", "phone connection fully closed")
hook = nil

-- captive dns: an A query is answered with our ip, unicast to the asker -------

t, sent, rxq = 0, {}, {}
arp.cache = {}
local i3 = iface.new(drv)
i3.ip = IP_A
i3:dnsd()
-- DNS A query for "a.com" from IP_B:5353 -> IP_A:53
local DNSQ = H[[0001 0100 0001 0000 0000 0000 0161 0363 6f6d 00 0001 0001]]
push(link.encap(link.ETH_IP, ipv4.build{src = IP_B, dst = IP_A,
      proto = ipv4.UDP, payload = udp.build(IP_B, 5353, IP_A, 53, DNSQ)}))
i3:poll(0)
mac, f = lastf()
eq(mac, MAC_B, "dns reply unicast back to the asker")
local rp = ipv4.parse(select(2, link.decap(f)))
eq(rp.proto, ipv4.UDP, "dns reply is udp")
eq(rp.dst, IP_B, "dns reply addressed to the asker")
local dd = udp.parse(rp)
eq(dd.sport, 53, "dns reply from port 53")
eq(dd.dport, 5353, "dns reply to the query's source port")
eq(dd.payload:sub(-4), IP_A, "dns answer resolves to our ip")
