-- net.iface - the glue that binds the driver to the protocol modules (#217):
-- frame demux (ARP/ICMP/UDP/TCP), passive MAC learning, and the blocking
-- convenience flows the hardware DoD uses from the REPL:
--
--   ifc = net.iface.new(net.iface.nabdrv())      -- after nab.wifi(ssid, psk)
--   print(ifc:dhcp(15000) and net.link.ntoa(ifc.ip))
--   status, body = ifc:http_get(net.link.ip("192.168.0.10"), "srv", "/app.lc")
--
-- Unresolved-MAC sends emit an ARP request and drop the packet: TCP's
-- retransmit (and DHCP's broadcast) covers the gap, so there is no tx queue.
--
-- THIS FILE IS THE BOOT PATH (#219) and is frozen into flash: join, get an
-- address, fetch one file. Everything that only a *listening* rabbit needs -
-- the DHCP and DNS servers, the HTTP server, and the :resolve/:ntp question
-- flows - lives in iface_x.lua, which is loaded over the REPL when something
-- wants it. Splitting them is what keeps the resident image inside its budget;
-- the two halves meet at `iface.mt`, the shared method table below, and at the
-- :tcpin seam that iface_x wraps to add listen-socket routing.

net = net or {}
local iface = {}
net.iface = iface
local link, arp, ipv4, udp, dhcp, tcp, http =
  net.link, net.arp, net.ipv4, net.udp, net.dhcp, net.tcp, net.http

local BCAST_IP = "\255\255\255\255"

-- The method table every interface shares. It used to be one set of closures
-- built per instance inside iface.new; they were already written `self`-style,
-- so hoisting them here costs no behaviour, saves a closure set per interface,
-- and - the point of the change - gives iface_x somewhere to add a method.
local mt = {}
mt.__index = mt
iface.mt = mt

local function subnet_eq(a, b, m)
  for k = 1, 4 do
    if (a:byte(k) & m:byte(k)) ~= (b:byte(k) & m:byte(k)) then return false end
  end
  return true
end

-- the real driver: nab.wifi (or nab.wifi_ap) must be up first
function iface.nabdrv()
  return {mac = nab.wifi_mac(), time = nab.time,
          send = nab.wifi_send, recv = nab.wifi_recv}
end

-- drv = {mac=,time=,send=fn(dst_mac,frame),recv=fn(ms)->src_mac,frame|nil}
function iface.new(drv)
  return setmetatable(
    {drv = drv, mac = drv.mac, time = drv.time, udp_ports = {}}, mt)
end

-- IPv4 packet out: broadcast, cached MAC, or via the router; unknown MAC
-- triggers an ARP request instead of the send (see header note)
function mt:ipsend(dst_ip, pkt)
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
function mt:pump(c, out)
  for _, p in ipairs(out or c:poll()) do
    self:ipsend(c.dst, p)
  end
end

-- Inbound TCP segment. The boot path has exactly one connection at a time -
-- self.conn, the client socket :http_get opens - so that is all this knows.
-- iface_x wraps it to route into the listen/established slots :serve keeps.
function mt:tcpin(pkt)
  if self.conn then self:pump(self.conn, self.conn:input(pkt)) end
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
  elseif pkt.proto == ipv4.TCP then
    self:tcpin(pkt)
  end
end

-- one receive/timer slice; the building block of every blocking flow
function mt:poll(ms)
  local src, f = self.drv.recv(ms or 0)
  if src then dispatch(self, src, f) end
  if self.conn then self:pump(self.conn) end
  -- self.conns is iface_x's :serve slot list; the nil guard is all the boot
  -- path pays to keep the timer half of serve working without overriding :poll
  if self.conns then
    for _, slot in ipairs(self.conns) do self:pump(slot.c) end
  end
end

-- DHCP join: blocks up to timeout ms, then i.ip/mask/router are set.
-- -> lease | nil, err
function mt:dhcp(timeout)
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
  self.dns = got.dns -- resolver for iface_x's :resolve; assign after :dhcp to override
  return got
end

-- Blocking GET. dst_ip nil resolves `host` first (#232) - hostname or dotted
-- quad; pass an explicit dst_ip to skip DNS entirely. `host` always feeds the
-- Host header. The timeout bounds the HTTP exchange; a lookup adds its own
-- default budget on top. -> status, body | nil, err
--
-- :resolve is iface_x's, so a boot image that never loaded it must be handed an
-- address - which is exactly what #219's config URL carries, and why dns.lua
-- costs the resident set nothing.
function mt:http_get(dst_ip, host, path, timeout)
  if not dst_ip then
    if not self.resolve then return nil, "no resolver (net.iface_x not loaded)" end
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
