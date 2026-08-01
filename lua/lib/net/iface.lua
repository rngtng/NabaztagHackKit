-- net.iface - the glue that binds the driver to the protocol modules (#217):
-- frame demux (ARP/ICMP/UDP/TCP), passive MAC learning, and the blocking
-- convenience flows the hardware DoD uses from the REPL:
--
--   ifc = net.iface.new(net.iface.nabdrv())      -- after nab.wifi(ssid, psk)
--   print(ifc:dhcp(15000) and net.link.ntoa(ifc.ip))
--   status, body = ifc:http_get(net.link.ip("192.168.0.10"), "srv", "/app.lc")
--
-- AP config mode (#218): ifc:dhcpd{...} + ifc:serve(80, handler).
--
-- Unresolved-MAC sends emit an ARP request and drop the packet: TCP's
-- retransmit (and DHCP's broadcast) covers the gap, so there is no tx queue.

net = net or {}
local iface = {}
net.iface = iface
local link, arp, ipv4, udp, dhcp, tcp, http, dns =
  net.link, net.arp, net.ipv4, net.udp, net.dhcp, net.tcp, net.http, net.dns

local BCAST_IP = "\255\255\255\255"

-- serve() concurrency (#286): a joined phone's captive-portal check opens
-- several TCP connections in parallel (one per DNS-hijacked hostname it
-- probes), not one at a time - keep this small, each slot is a full tcp
-- connection object plus an http.request() parser.
local MAX_CONNS = 3

-- the real driver: nab.wifi (or nab.wifi_ap) must be up first
function iface.nabdrv()
  return {mac = nab.wifi_mac(), time = nab.time,
          send = nab.wifi_send, recv = nab.wifi_recv}
end

-- drv = {mac=,time=,send=fn(dst_mac,frame),recv=fn(ms)->src_mac,frame|nil}
function iface.new(drv)
  local i = {drv = drv, mac = drv.mac, time = drv.time, udp_ports = {}}

  local function subnet_eq(a, b, m)
    for k = 1, 4 do
      if (a:byte(k) & m:byte(k)) ~= (b:byte(k) & m:byte(k)) then return false end
    end
    return true
  end

  -- IPv4 packet out: broadcast, cached MAC, or via the router; unknown MAC
  -- triggers an ARP request instead of the send (see header note)
  function i:ipsend(dst_ip, pkt)
    if dst_ip == BCAST_IP then
      return self.drv.send(link.BCAST, link.encap(link.ETH_IP, pkt))
    end
    local hop = dst_ip
    if self.router and self.mask
       and not subnet_eq(dst_ip, self.ip, self.mask) then
      hop = self.router
    end
    local mac = arp.cache[hop]
    if mac then
      return self.drv.send(mac, link.encap(link.ETH_IP, pkt))
    end
    self.drv.send(link.BCAST, arp.request(self.mac, self.ip, hop))
  end

  -- drive a tcp connection: send its pending output (default: timer poll)
  function i:pump(c, out)
    for _, p in ipairs(out or c:poll()) do
      self:ipsend(c.dst, p)
    end
  end

  local function dispatch(self, src_mac, frame)
    local et, p = link.decap(frame)
    if et == link.ETH_ARP then
      local a = arp.parse(p)
      if a then
        local reply = arp.input(a, self.mac, self.ip)
        if reply then self.drv.send(a.sha, reply) end
      end
      return
    end
    if et ~= link.ETH_IP then return end
    local pkt = ipv4.parse(p)
    if not pkt then return end
    if self.ip and pkt.dst ~= self.ip and pkt.dst ~= BCAST_IP then return end
    arp.learn(pkt.src, src_mac) -- passive learning; replies need no ARP trip
    if pkt.proto == ipv4.ICMP and pkt.dst == self.ip then
      local r = ipv4.icmp_input(pkt)
      if r then self.drv.send(src_mac, link.encap(link.ETH_IP, r)) end
    elseif pkt.proto == ipv4.UDP then
      local d = udp.parse(pkt)
      local h = d and self.udp_ports[d.dport]
      if h then h(d, pkt) end
    elseif pkt.proto == ipv4.TCP and self.conn then
      self:pump(self.conn, self.conn:input(pkt))
    elseif pkt.proto == ipv4.TCP and self.conns then
      -- route to the matching established connection, or the one slot still
      -- in "listen" (there is ever at most one - serve() opens the next as
      -- soon as this one accepts a SYN) if this is a fresh SYN
      local s = tcp.parse(pkt)
      if s then
        for _, slot in ipairs(self.conns) do
          local c = slot.c
          if c.state == "listen" then
            if (s.flags & tcp.SYN) ~= 0 and (s.flags & tcp.ACK) == 0 then
              self:pump(c, c:input(pkt))
              break
            end
          elseif c.dst == pkt.src and c.dport == s.sport then
            self:pump(c, c:input(pkt))
            break
          end
        end
      end
    end
  end

  -- one receive/timer slice; the building block of every blocking flow
  function i:poll(ms)
    local src, f = self.drv.recv(ms or 0)
    if src then dispatch(self, src, f) end
    if self.conn then self:pump(self.conn) end
    if self.conns then
      for _, slot in ipairs(self.conns) do self:pump(slot.c) end
    end
  end

  -- DHCP join: blocks up to timeout ms, then i.ip/mask/router are set.
  -- -> lease | nil, err
  function i:dhcp(timeout)
    local c = dhcp.client(self.mac, self.time() ~ 0x5bd1)
    local got
    self.udp_ports[68] = function(d)
      local out, lease = c:input(d.payload)
      if out then self.drv.send(link.BCAST, out) end
      if lease then got = lease end
    end
    self.drv.send(link.BCAST, c:discover())
    local t0, last = self.time(), self.time()
    while not got do
      if self.time() - t0 > (timeout or 15000) then
        self.udp_ports[68] = nil
        return nil, "dhcp timeout"
      end
      self:poll(100)
      if self.time() - last > 2000 then -- re-broadcast the in-flight step
        last = self.time()
        self.drv.send(link.BCAST, c:retransmit())
      end
    end
    self.udp_ports[68] = nil
    self.ip, self.mask, self.router = got.ip, got.mask, got.router
    self.dns = got.dns -- resolver for i:resolve; assign after :dhcp to override
    return got
  end

  -- single-lease DHCP server for AP config mode (#218)
  function i:dhcpd(o)
    local s = dhcp.server(o)
    self.ip, self.mask = o.ip, o.mask
    self.udp_ports[67] = function(d)
      local f = s:input(d.payload)
      if f then self.drv.send(link.BCAST, f) end
    end
  end

  -- captive-portal DNS: answer every A query with `ip` (default: our own),
  -- unicast back to the asker, so a joined phone resolves its OS connectivity
  -- probe to the portal and shows the config page (#233 follow-up). Register
  -- alongside dhcpd before serve(); the reply rides the same poll loop.
  function i:dnsd(ip)
    local s = dns.server(ip or self.ip)
    self.udp_ports[53] = function(d, pkt)
      local r = s:input(d.payload)
      if r then
        self:ipsend(pkt.src, ipv4.build{src = self.ip, dst = pkt.src,
          proto = ipv4.UDP,
          payload = udp.build(self.ip, 53, pkt.src, d.sport, r)})
      end
    end
  end

  -- Blocking DNS A lookup (#232), mirroring :dhcp - one UDP question, retried
  -- on a 1 s timer with a fresh transaction id until `timeout` (default 5 s).
  -- The server is self.dns, learned from the DHCP lease; assign the field to
  -- override it (a persisted config key is #268). A dotted quad resolves to
  -- itself, so a caller can hand either form straight through.
  -- -> 4-byte ip | nil, err
  function i:resolve(host, timeout)
    local literal = link.aton(host)
    if literal then return literal end
    local hit = dns.cached(host, self.time())
    if hit then return hit end
    if not self.ip then return nil, "no address" end
    if not self.dns then return nil, "no dns server" end
    local server = self.dns
    local sport = 49152 + (self.time() & 0x3FFF)
    local id, got, ttl
    local function ask()
      -- fresh id per attempt: a late answer to the previous one no longer
      -- matches, so a retry can never adopt a stale reply
      id = string.pack(">I2", (self.time() ~ 0x5bd1) & 0xFFFF)
      local q, qerr = dns.query(id, host)
      if not q then return qerr end
      self:ipsend(server, ipv4.build{src = self.ip, dst = server,
        proto = ipv4.UDP,
        payload = udp.build(self.ip, sport, server, 53, q)})
    end
    local bad = ask()
    if bad then return nil, bad end
    local err, done
    self.udp_ports[sport] = function(d, pkt)
      if pkt.src ~= server or d.sport ~= 53 then return end
      local ip, t, definitive = dns.answer(d.payload, id, host)
      if ip then got, ttl, done = ip, t, true
      -- A definitive negative (NXDOMAIN, TC, no A) is our answer: stop now with
      -- its error instead of retrying to the full timeout. Anything dns.answer
      -- is unsure is ours (wrong id, spoofed question) leaves `done` unset, so
      -- the loop keeps listening.
      elseif definitive then err, done = t, true end
    end
    local t0, last = self.time(), self.time()
    while not done do
      if self.time() - t0 > (timeout or 5000) then
        self.udp_ports[sport] = nil
        return nil, "dns timeout"
      end
      self:poll(100)
      if not done and self.time() - last > 1000 then
        last = self.time()
        ask() -- the first send may also have been eaten by the ARP round trip
      end
    end
    self.udp_ports[sport] = nil
    if not got then return nil, err end
    dns.remember(host, got, ttl, self.time())
    return got
  end

  -- Blocking GET. dst_ip nil resolves `host` first (#232) - hostname or dotted
  -- quad; pass an explicit dst_ip to skip DNS entirely. `host` always feeds the
  -- Host header. The timeout bounds the HTTP exchange; a lookup adds its own
  -- default budget on top. -> status, body | nil, err
  function i:http_get(dst_ip, host, path, timeout)
    if not dst_ip then
      local err
      dst_ip, err = self:resolve(host)
      if not dst_ip then return nil, err end
    end
    local c = tcp.client{src = self.ip, dst = dst_ip, dport = 80,
                         sport = 49152 + (self.time() & 0x3FFF),
                         clock = self.time}
    self.conn = c
    local r = http.response()
    self:pump(c, c:connect())
    c:send(http.get(host, path)) -- queued; flushed once established
    local t0 = self.time()
    while not r.done do
      if self.time() - t0 > (timeout or 20000) or c.err then
        self.conn = nil
        return nil, c.err or "http timeout"
      end
      self:poll(50)
      local d = c:read()
      if d ~= "" then r:feed(d) end
      if c.state == "close-wait" or c.state == "closed" then
        r:eof()
        break
      end
    end
    self:pump(c, c:close()) -- best-effort polite teardown
    self.conn = nil
    if not r.done then return nil, "closed early" end
    return r.status, r.body
  end

  -- HTTP server (the config portal), up to MAX_CONNS connections at once
  -- (#286). handler(q) -> body [, status [, stop]]; returns once a handler
  -- sets stop and every connection accepted so far has finished its own
  -- close - a handful of parallel captive-portal probes each get served,
  -- not just the first while the rest sit on an unanswered SYN.
  function i:serve(port, handler)
    self.conns = {}
    local stop
    local function accept()
      if stop or #self.conns >= MAX_CONNS then return end
      local c = tcp.listen{src = self.ip, port = port, clock = self.time}
      self.conns[#self.conns + 1] = {c = c, q = http.request(), phase = "accept"}
    end
    accept() -- always keep one socket ready for the next SYN
    while true do
      self:poll(50)
      local n = 1
      while n <= #self.conns do
        local s = self.conns[n]
        local c = s.c
        if s.phase == "accept" then
          if stop and c.state == "listen" then
            s.phase = "done" -- portal finished; drop this idle spare listener
          elseif c.state ~= "listen" then -- this slot took a SYN; open the next
            accept()
            s.t0, s.phase = self.time(), "req"
          end
        elseif s.phase == "req" then
          local d = c:read()
          if d ~= "" then s.q:feed(d) end
          if s.q.done then
            s.phase = "resp"
          elseif c.state == "closed" or self.time() - s.t0 > 30000 then
            s.phase = "done" -- half-open peer: abandon
          end
        elseif s.phase == "resp" then
          local body, status, hstop = handler(s.q)
          if hstop then stop = true end
          self:pump(c, c:send(http.response_build(status or "200 OK", body)))
          s.t0, s.phase = self.time(), "close"
        elseif s.phase == "close" then
          if not c.rtx
             and (c.state == "established" or c.state == "close-wait") then
            self:pump(c, c:close())
          end
          if (not c.rtx and c.state == "closed")
             or self.time() - s.t0 > 5000 then
            s.phase = "done"
          end
        end
        if s.phase == "done" then
          table.remove(self.conns, n)
        else
          n = n + 1
        end
      end
      if stop and #self.conns == 0 then break end
    end
    self.conns = nil
  end

  return i
end
