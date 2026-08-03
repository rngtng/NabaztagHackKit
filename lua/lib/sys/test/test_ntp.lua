-- sys.ntp: the SNTP v4 request/reply (#259), plus net.iface's :ntp transport.
--
-- Packet fixtures come from an independent Python generator (scratchpad
-- ntpfix.py: struct.pack straight off RFC 4330's field layout, epochs from
-- CPython's datetime) - nothing below is derived from the module under test.
-- The scripted server further down rebuilds a reply itself, and is pinned to
-- the generator's bytes first so it cannot drift into agreeing with ntp.lua.

local ntp = sys.ntp

local COOKIE = H"deadbeef0badc0de"
local T = 1785587696 -- 2026-08-01T12:34:56Z

local REQUEST = H[[23000000000000000000000000000000
                   00000000000000000000000000000000
                   0000000000000000deadbeef0badc0de]]
local REPLY = H[[240206ec000000000000000000000000
                 ee18666f00000000deadbeef0badc0de
                 ee18667000000000ee18667000000000]]
local REPLY_FRAC = H[[240206ec000000000000000000000000
                      ee18666f00000000deadbeef0badc0de
                      ee18667000000000ee18667080000000]]
local REPLY_NOCOOK = H[[240206ec000000000000000000000000
                        ee18666f000000000000000000000000
                        ee18667000000000ee18667000000000]]
local REPLY_KOD = H[[240006ec000000000000000000000000
                     ee18666f00000000deadbeef0badc0de
                     ee18667000000000ee18667000000000]]
local REPLY_ST16 = H[[241006ec000000000000000000000000
                      ee18666f00000000deadbeef0badc0de
                      ee18667000000000ee18667000000000]]
local REPLY_ALARM = H[[e40206ec000000000000000000000000
                       ee18666f00000000deadbeef0badc0de
                       ee18667000000000ee18667000000000]]
local REPLY_V2 = H[[140206ec000000000000000000000000
                    ee18666f00000000deadbeef0badc0de
                    ee18667000000000ee18667000000000]]
local REPLY_V3 = H[[1c0206ec000000000000000000000000
                    ee18666f00000000deadbeef0badc0de
                    ee18667000000000ee18667000000000]]
local REPLY_ISREQ = H[[230206ec000000000000000000000000
                       ee18666f00000000deadbeef0badc0de
                       ee18667000000000ee18667000000000]]
local REPLY_BCAST = H[[250206ec000000000000000000000000
                       ee18666f00000000deadbeef0badc0de
                       ee18667000000000ee18667000000000]]
local REPLY_UNSET = H[[240206ec000000000000000000000000
                       ffffffff00000000deadbeef0badc0de
                       00000000000000000000000000000000]]
-- NTP seconds 3600, i.e. one hour into era 1 (the counter wrapped on
-- 2036-02-07T06:28:16Z) -> Unix 2085982096 = 2036-02-07T07:28:16Z
local REPLY_ERA1 = H[[240206ec000000000000000000000000
                      00000e0f00000000deadbeef0badc0de
                      00000e100000000000000e1000000000]]

-- build ----------------------------------------------------------------------

eq(ntp.build(COOKIE), REQUEST, "the request matches the generator's bytes")
eq(#ntp.build(), 48, "an SNTP request is 48 bytes")
eq(ntp.build():byte(1), 0x23, "LI 0, version 4, mode 3 (client)")
eq(ntp.build():sub(2), ("\0"):rep(47), "every other field of a request is zero")
eq(ntp.build(COOKIE):sub(41), COOKIE, "the cookie lands in transmit timestamp")
eq(ntp.build("short"), nil, "a cookie that is not 8 bytes is refused")
eq(select(2, ntp.build("short")), "bad cookie", "and says why")
eq(ntp.build(42), nil, "a non-string cookie is refused")

-- parse: the happy paths -----------------------------------------------------

local r = ntp.parse(REPLY, COOKIE)
eq(r and r.epoch, T, "the reply parses to 2026-08-01T12:34:56Z")
eq(r.ms, 0, "a zero fraction is 0 ms")
eq(r.stratum, 2, "the stratum is reported")
eq(r.li, 0, "no leap warning")
eq(sys.time.format(r.epoch), "2026-08-01T12:34:56Z", "and formats as expected")
eq(ntp.parse(REPLY).epoch, T, "the cookie check is optional")
eq(ntp.parse(REPLY_NOCOOK).epoch, T, "so a server that zeroes originate works")
eq(ntp.parse(REPLY_FRAC, COOKIE).ms, 500, "a half-second fraction is 500 ms")
eq(ntp.parse(REPLY_FRAC, COOKIE).epoch, T, "and does not disturb the seconds")
eq(ntp.parse(REPLY_V3, COOKIE).epoch, T, "an SNTPv3 server is accepted")
eq(ntp.parse(REPLY_BCAST, COOKIE).epoch, T, "mode 5 (broadcast) is accepted")
eq(ntp.parse(REPLY .. ("\0"):rep(20), COOKIE).epoch, T,
   "trailing bytes (an authenticator) are ignored")

-- The mod-2^32 conversion carries the era-1 rollover with no special case:
-- both the server's counter and our subtraction wrap, and they cancel.
eq(ntp.parse(REPLY_ERA1, COOKIE).epoch, 2085982096,
   "an era-1 timestamp resolves to 2036-02-07T07:28:16Z")
eq(sys.time.format(ntp.parse(REPLY_ERA1, COOKIE).epoch),
   "2036-02-07T07:28:16Z", "and formats as a 2036 date, not a 1900 one")

-- parse: everything that must be refused ---------------------------------------

local function refused(p, label, cookie)
  local got, err = ntp.parse(p, cookie)
  eq(got, nil, label)
  ok(type(err) == "string" and #err > 0, label .. " reports why")
  return err
end

eq(refused(REPLY:sub(1, 47), "a 47-byte datagram"), "short ntp",
   "48 bytes is the minimum")
eq(refused("", "an empty datagram"), "short ntp", "and an empty one too")
eq(refused(nil, "a nil datagram"), "short ntp", "a non-string is not a packet")
eq(refused(REPLY_ISREQ, "mode 3 - our own request looped back"),
   "not a server reply", "only a server mode is trusted")
eq(refused(REPLY_V2, "an NTPv2 server"), "bad version", "v3/v4 only")
eq(refused(REPLY_ALARM, "LI 3 - the server's own clock is unset"),
   "unsynchronised", "the leap-indicator alarm is honoured")
eq(refused(REPLY_KOD, "stratum 0 - a kiss-of-death packet"), "kiss of death",
   "a KoD is never read as a time")
eq(refused(REPLY_ST16, "stratum 16 - unsynchronised"), "bad stratum",
   "stratum must be 1..15")
eq(refused(REPLY_UNSET, "an all-zero transmit timestamp"), "no transmit time",
   "a server that never filled the field is not a clock")
eq(refused(REPLY, "a reply echoing someone else's cookie", H"0011223344556677"),
   "wrong originate", "the originate timestamp is matched against the cookie")

-- ifc:ntp - the blocking flow over a scripted server ---------------------------

local link, arp, ipv4, udp, iface =
  net.link, net.arp, net.ipv4, net.udp, net.iface
local MAC_A, MAC_B = H"00095b8f3a01", H"d83add112233"
local IP_A, SERVER = H"c0a8002a", H"c0a80001"

local t, sent, rxq, hook = 0, {}, {}, nil
local drv = {
  mac = MAC_A, time = function() return t end,
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

-- An SNTP server written here, not called out of ntp.lua - and pinned to the
-- generator's fixture below before anything uses it. Unix -> NTP seconds is
-- +2208988800, which does not fit a 32-bit signed integer, so it is added as
-- its mod-2^32 image - the same wrap the fixture bytes were generated with.
local TO_NTP = 0x83AA7E80
local function serve(unix, cookie, first, stratum)
  local ts = string.pack(">I4", unix + TO_NTP)
  return string.pack(">BBbb", first or 0x24, stratum or 2, 6, -20)
         .. ("\0"):rep(12) .. string.pack(">I4", unix - 1 + TO_NTP)
         .. ("\0"):rep(4) .. cookie .. ts .. ("\0"):rep(4) .. ts .. ("\0"):rep(4)
end
eq(serve(T, COOKIE), REPLY, "the test's own server reproduces the fixture")

local function answer(opts)
  opts = opts or {}
  return function(_, f)
    local et, p = link.decap(f)
    if et ~= link.ETH_IP then return end
    local pkt = ipv4.parse(p)
    if not pkt or pkt.proto ~= ipv4.UDP then return end
    local d = udp.parse(pkt)
    if not d or d.dport ~= 123 then return end
    local body = serve(opts.unix or T, opts.cookie or d.payload:sub(41),
                       opts.first, opts.stratum)
    rxq[#rxq + 1] = {MAC_B, link.encap(link.ETH_IP, ipv4.build{
      src = SERVER, dst = IP_A, proto = ipv4.UDP,
      payload = udp.build(SERVER, 123, IP_A, d.sport, body)})}
  end
end

local function fresh()
  t, sent, rxq = 0, {}, {}
  arp.reset()
  arp.cache[SERVER] = MAC_B
  local i = iface.new(drv)
  i.ip = IP_A
  return i
end

local i = fresh()
hook = answer()
local epoch, extra = i:ntp(SERVER)
eq(epoch, T, "ifc:ntp returns the server's Unix seconds")
eq(extra, nil, "and nothing else, so sys.time.set(ifc:ntp(...)) binds right")
eq(next(i.udp_ports), nil, "the ephemeral port is unregistered afterwards")
local q = udp.parse(ipv4.parse(select(2, link.decap(sent[1].f))))
eq(q.dport, 123, "the question goes to port 123")
ok(q.sport >= 49152, "from an ephemeral source port")
eq(#q.payload, 48, "and is a 48-byte SNTP request")
eq(q.payload:byte(1), 0x23, "in client mode")

-- the DoD line: one query sets a real wall clock
sys.time.reset()
eq(sys.time.set(i:ntp(SERVER)), T, "sys.time.set(ifc:ntp(...)) sets the clock")
eq(sys.time.format(sys.time.now(0)), "2026-08-01T12:34:56Z",
   "and the rabbit can print the date")
sys.time.reset()

-- a reply echoing the wrong cookie is ignored; the 1 s retry gets a good one
i = fresh()
local attempts = 0
hook = function(mac, f)
  attempts = attempts + 1
  answer(attempts == 1 and {cookie = H"0000000000000000"} or nil)(mac, f)
end
eq(i:ntp(SERVER), T, "a stale reply is skipped and the retry is accepted")
ok(attempts >= 2, "which took a retransmit")

-- Fresh cookie per attempt (as :resolve re-rolls its transaction id): this
-- server always answers with the *previous* attempt's cookie, i.e. every reply
-- is a late one. Reusing a single cookie across retries would install a stale
-- reading here; instead nothing is ever adopted.
i = fresh()
local prev, cookies = H"0000000000000000", {}
hook = function(mac, f)
  local d = udp.parse(ipv4.parse(select(2, link.decap(f))))
  local cur = d.payload:sub(41)
  cookies[#cookies + 1] = cur
  answer{cookie = prev}(mac, f)
  prev = cur
end
epoch, err = i:ntp(SERVER, 3000)
eq(epoch, nil, "a reply echoing a superseded cookie is never adopted")
eq(err, "ntp timeout", "so a stale time is refused, not installed")
ok(#cookies >= 2, "which took more than one attempt")
ok(cookies[1] ~= cookies[2], "and every attempt carried a fresh cookie")

-- a definitive refusal stops immediately, before the timeout
i = fresh()
hook = answer{stratum = 0}
local err
epoch, err = i:ntp(SERVER)
eq(epoch, nil, "a kiss-of-death server yields no time")
eq(err, "kiss of death", "and the reason reaches the caller")
ok(t < 5000, "without waiting out the timeout")

-- silence times out
i = fresh()
hook = nil
epoch, err = i:ntp(SERVER, 3000)
eq(epoch, nil, "a server that never answers yields no time")
eq(err, "ntp timeout", "and reports the timeout")
eq(next(i.udp_ports), nil, "the port is unregistered on timeout too")
ok(#sent >= 3, "the request was retransmitted about once a second")

-- a hostname: :resolve and then the SNTP exchange, over the same fake wire
-- (the sinkhole server answers every A query with SERVER's own address)
net.dns.forget()
i = fresh()
i.dns = SERVER
local dnsd = net.dns.server(SERVER)
hook = function(mac, f)
  local et, p = link.decap(f)
  if et ~= link.ETH_IP then return end
  local pkt = ipv4.parse(p)
  if not pkt or pkt.proto ~= ipv4.UDP then return end
  local d = udp.parse(pkt)
  if d and d.dport == 53 then
    local r = dnsd:input(d.payload)
    rxq[#rxq + 1] = {MAC_B, link.encap(link.ETH_IP, ipv4.build{
      src = SERVER, dst = IP_A, proto = ipv4.UDP,
      payload = udp.build(SERVER, 53, IP_A, d.sport, r)})}
  else
    answer()(mac, f)
  end
end
eq(i:ntp("ntp.example"), T, "a hostname is resolved first, then queried")
eq(net.dns.cached("ntp.example", 0), SERVER, "and the lookup really happened")

-- preconditions
i = fresh()
i.ip = nil
eq(select(2, i:ntp(SERVER)), "no address", "no lease, no NTP")
i = fresh()
eq(select(2, i:ntp("pool.ntp.org")), "no dns server",
   "a hostname goes through :resolve, which needs a resolver")
eq(select(2, i:ntp()), "bad server", "no server at all is an error, not a crash")
