-- net.iface_x - everything a *listening* or *asking* rabbit needs, split off
-- iface.lua for #219. None of it is on the boot path (join, address, fetch one
-- file), so none of it is frozen into flash: load this over the REPL, or from
-- the app, when you want a server or a name.
--
--   :dhcpd{...}  single-lease DHCP server   (AP config mode, #218/#233)
--   :dnsd([ip])  captive-portal DNS sinkhole            (#233 follow-up)
--   :serve(p,h)  HTTP server, up to MAX_CONNS at once          (#286/#302)
--   :resolve(h)  blocking DNS A lookup                              (#232)
--   :ntp(s)      blocking SNTP query                               (#259)
--
-- Load order: after iface.lua (it extends iface.mt), plus dhcp/dns/tcp/http
-- for whichever of the five you actually call.

net = net or {}
local mt = net.iface.mt
local link, ipv4, udp, dhcp, tcp, http, dns =
  net.link, net.ipv4, net.udp, net.dhcp, net.tcp, net.http, net.dns

-- serve() concurrency (#286): a joined phone's captive-portal check opens
-- several TCP connections in parallel (one per DNS-hijacked hostname it
-- probes), not one at a time - keep this small, each slot is a full tcp
-- connection object plus an http.request() parser.
local MAX_CONNS = 3

-- The blocking UDP request/reply loop both question flows share (:resolve,
-- :ntp): register a demux handler on our ephemeral port, send, then poll and
-- re-ask on a 1 s timer until an answer arrives or `timeout` (default 5 s)
-- runs out. The port is unregistered on every exit path.
--
--   ask()         -> nil, or an error string that aborts before sending
--   judge(d, pkt) -> the answer, or nil + err for a *definitive* refusal.
--                    Plain nil keeps waiting - that is what lets a datagram
--                    which is not ours (wrong id, wrong cookie, another
--                    sender) be ignored instead of ending the lookup early.
-- -> answer | nil, err
local function query(self, sport, ask, judge, timeout, label)
  local got, err, done
  self.udp_ports[sport] = function(d, pkt)
    local v, e = judge(d, pkt)
    if v then got, done = v, true
    elseif e then err, done = e, true end
  end
  err = ask()
  local t0, last = self.time(), self.time()
  while not err and not done do
    if self.time() - t0 > (timeout or 5000) then
      err = label
      break
    end
    self:poll(100)
    if not done and self.time() - last > 1000 then
      last = self.time()
      ask() -- the first send may also have been eaten by the ARP round trip
    end
  end
  self.udp_ports[sport] = nil
  if got then return got end
  return nil, err
end

-- Listen-socket routing, added to iface.mt's one-connection :tcpin. Wrapping
-- rather than replacing keeps the boot path's self.conn branch defined in one
-- place (the same shape boot.lua uses to wrap nab.on).
local base_tcpin = mt.tcpin

function mt:tcpin(pkt)
  if not self.conns then return base_tcpin(self, pkt) end
  -- route to the matching established connection, or the one slot still
  -- in "listen" (there is ever at most one - serve() opens the next as
  -- soon as this one accepts a SYN) if this is a fresh SYN
  local s = tcp.parse(pkt)
  if not s then return end
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

-- single-lease DHCP server for AP config mode (#218)
function mt:dhcpd(o)
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
function mt:dnsd(ip)
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
function mt:resolve(host, timeout)
  local literal = link.aton(host)
  if literal then return literal end
  local hit = dns.cached(host, self.time())
  if hit then return hit end
  if not self.ip then return nil, "no address" end
  if not self.dns then return nil, "no dns server" end
  local server = self.dns
  local sport = 49152 + (self.time() & 0x3FFF)
  local id, ttl
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
  local got, err = query(self, sport, ask, function(d, pkt)
    if pkt.src ~= server or d.sport ~= 53 then return end
    local ip, t, definitive = dns.answer(d.payload, id, host)
    -- A definitive negative (NXDOMAIN, TC, no A) is our answer: stop now with
    -- its error instead of retrying to the full timeout. Anything dns.answer
    -- is unsure is ours (wrong id, spoofed question) returns plain nil, so
    -- the loop keeps listening.
    if ip then ttl = t; return ip end
    if definitive then return nil, t end
  end, timeout, "dns timeout")
  if not got then return nil, err end
  dns.remember(host, got, ttl, self.time())
  return got
end

-- Blocking SNTP query (#259), shaped like :resolve - one UDP datagram to
-- port 123 from an ephemeral port, retried on a 1 s timer until `timeout`
-- (default 5 s), carrying a cookie the reply must echo in its originate
-- timestamp. `server` is a 4-byte IP (link.ip) or a name/dotted quad, which
-- is resolved first. Needs sys/ntp.lua loaded; net owns no clock, so the
-- caller stores the reading: sys.time.set(ifc:ntp(server)). Seconds are all
-- that comes back - one return value, so that idiom cannot mis-bind - and
-- the sub-second fraction stays available from sys.ntp.parse.
-- -> unix epoch seconds | nil, err
function mt:ntp(server, timeout)
  local ntp = sys and sys.ntp
  if not ntp then return nil, "sys.ntp not loaded" end
  if type(server) ~= "string" then return nil, "bad server" end
  if not self.ip then return nil, "no address" end
  local ip = server
  if #server ~= 4 then -- anything but a 4-byte binary address is a name
    local rerr
    ip, rerr = self:resolve(server)
    if not ip then return nil, rerr end
  end
  local sport = 49152 + (self.time() & 0x3FFF)
  local req
  local function ask()
    -- fresh cookie per attempt, for the same reason :resolve re-rolls its
    -- transaction id: a late reply to the previous attempt no longer
    -- matches, so a retry can never adopt a stale - and by then wrong - time
    req = ntp.build(string.pack(">I4I4", self.time(), self.time() ~ 0x5bd1))
    self:ipsend(ip, ipv4.build{src = self.ip, dst = ip, proto = ipv4.UDP,
      payload = udp.build(self.ip, sport, ip, ntp.PORT, req)})
  end
  local got, err = query(self, sport, ask, function(d, pkt)
    if pkt.src ~= ip or d.sport ~= ntp.PORT then return end
    local r, perr = ntp.parse(d.payload, req:sub(41))
    -- A reply failing the cookie is not ours (a stale answer to an earlier
    -- attempt, or a spoof): plain nil keeps us listening. Anything else is a
    -- real refusal from our server - stop now, do not retry to the timeout.
    if r then return r end
    if perr ~= "wrong originate" then return nil, perr end
  end, timeout, "ntp timeout")
  if not got then return nil, err end
  return got.epoch
end

-- HTTP server (the config portal), up to MAX_CONNS connections at once
-- (#286). handler(q) -> body [, status [, stop]]; returns once a handler
-- sets stop and every connection accepted so far has finished its own
-- close - a handful of parallel captive-portal probes each get served,
-- not just the first while the rest sit on an unanswered SYN.
function mt:serve(port, handler)
  self.conns = {}
  local stop
  local function listening()
    for _, s in ipairs(self.conns) do
      if s.c.state == "listen" then return true end
    end
    return false
  end
  -- Opens a listener unless one is already waiting or we are at capacity.
  -- The `listening()` guard is what lets this be called from anywhere it
  -- might be needed without opening a second idle socket (#302).
  local function accept()
    if stop or #self.conns >= MAX_CONNS or listening() then return end
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
        -- A freed slot has to put the listener back (#302). accept() used to
        -- be reachable only from the "this slot took a SYN" branch above, and
        -- both call sites are capacity-guarded - so MAX_CONNS simultaneous
        -- connections left nothing in "listen", and when they finished
        -- nothing re-opened one. dispatch() routes a fresh SYN only to a
        -- listening slot, so from then on every connection was dropped while
        -- this loop spun on an empty list forever: its only exit is
        -- `stop and #conns == 0`, and stop comes from a handler that could no
        -- longer be reached. That is a phone's captive-portal burst, and the
        -- tap that would have opened the form is the connection lost.
        accept()
      else
        n = n + 1
      end
    end
    if stop and #self.conns == 0 then break end
  end
  self.conns = nil
end
