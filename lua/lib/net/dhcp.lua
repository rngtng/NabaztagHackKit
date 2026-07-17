-- net.dhcp - DHCP packet build/parse, join-mode client and the single-lease
-- server for AP config mode (#217, feeds #218).
--
-- The client emits complete SNAP frames (everything is broadcast until we
-- own an address); the caller sends them via nab.wifi_send(link.BCAST, f),
-- feeds UDP:68 payloads back through :input, and owns the retransmit timer
-- (:retransmit re-yields the in-flight frame). All replies here ride
-- broadcast (flags 0x8000) so the stack never needs to receive unicast IP
-- before it has an address.

net = net or {}
local dhcp = {}
net.dhcp = dhcp
local link, ipv4, udp = net.link, net.ipv4, net.udp

dhcp.DISCOVER, dhcp.OFFER, dhcp.REQUEST, dhcp.ACK, dhcp.NAK = 1, 2, 3, 5, 6
local MAGIC = "\99\130\83\99" -- 0x63825363
local ANY, BCAST = "\0\0\0\0", "\255\255\255\255"
local PARAMS = string.char(1, 3, 6) -- request mask + router + dns

-- {op=,xid=,mac=,msgtype=[,ciaddr,yiaddr,siaddr,opts={{code,data},...}]}
function dhcp.build(o)
  local t = {string.pack(">BBBBI4I2I2", o.op, 1, 6, 0, o.xid, 0, 0x8000),
             o.ciaddr or ANY, o.yiaddr or ANY, o.siaddr or ANY, ANY,
             o.mac, ("\0"):rep(10 + 64 + 128), MAGIC,
             string.pack("BBB", 53, 1, o.msgtype)}
  for _, op in ipairs(o.opts or {}) do
    t[#t + 1] = string.pack("BB", op[1], #op[2]) .. op[2]
  end
  t[#t + 1] = "\255"
  return table.concat(t)
end

-- -> {op=,xid=,ciaddr=,yiaddr=,siaddr=,mac=,msgtype=,opts={[code]=data}}
function dhcp.parse(p)
  if #p < 240 then return nil, "short dhcp" end
  if p:sub(237, 240) ~= MAGIC then return nil, "bad magic" end
  local r = {op = p:byte(1), xid = string.unpack(">I4", p, 5),
             ciaddr = p:sub(13, 16), yiaddr = p:sub(17, 20),
             siaddr = p:sub(21, 24), mac = p:sub(29, 34), opts = {}}
  local i = 241
  while i <= #p do
    local c = p:byte(i)
    if c == 255 then break end
    if c == 0 then -- pad
      i = i + 1
    else
      local l = p:byte(i + 1)
      if l == nil or i + 1 + l > #p then return nil, "bad option" end
      r.opts[c] = p:sub(i + 2, i + 1 + l)
      i = i + 2 + l
    end
  end
  if r.opts[53] then r.msgtype = r.opts[53]:byte(1) end
  return r
end

local function bcast_frame(src_ip, sport, dport, payload)
  return link.encap(link.ETH_IP, ipv4.build{
    src = src_ip, dst = BCAST, proto = ipv4.UDP,
    payload = udp.build(src_ip, sport, BCAST, dport, payload)})
end

-- Client: c:discover() -> frame; then per UDP:68 payload,
-- c:input(dgram) -> frame|nil, lease|nil. state: selecting/requesting/bound;
-- a NAK resets to init (call :discover() again). Pass a fresh xid (e.g. from
-- nab.time()) so retries after reboot don't collide.
function dhcp.client(mac, xid)
  local c = {state = "init", mac = mac, xid = xid or 0x3ab0071e}

  local function say(msgtype, opts)
    c.last = bcast_frame(ANY, 68, 67,
      dhcp.build{op = 1, xid = c.xid, mac = c.mac, msgtype = msgtype,
                 opts = opts})
    return c.last
  end

  function c:discover()
    self.state = "selecting"
    return say(dhcp.DISCOVER, {{55, PARAMS}})
  end

  function c:input(dgram)
    local r = dhcp.parse(dgram)
    if not r or r.op ~= 2 or r.xid ~= self.xid then return nil end
    if self.state == "selecting" and r.msgtype == dhcp.OFFER then
      self.state = "requesting"
      return say(dhcp.REQUEST,
                 {{50, r.yiaddr}, {54, r.opts[54] or r.siaddr}, {55, PARAMS}})
    elseif self.state == "requesting" and r.msgtype == dhcp.ACK then
      self.state = "bound"
      self.lease = {ip = r.yiaddr, mask = r.opts[1], router = r.opts[3],
                    dns = r.opts[6], server = r.opts[54] or r.siaddr,
                    ttl = r.opts[51] and string.unpack(">I4", r.opts[51])}
      return nil, self.lease
    elseif self.state == "requesting" and r.msgtype == dhcp.NAK then
      self.state = "init"
    end
  end

  function c:retransmit() return self.last end
  return c
end

-- Server for AP config mode: one fixed lease, DNS pointed at ourselves so
-- the phone's first lookup lands on the config portal (#218). A REQUEST for
-- any other address is NAKed back to rediscovery.
-- o = {ip=, client_ip=[, mask=]} -> s; s:input(udp:67 payload) -> frame|nil
function dhcp.server(o)
  local s = {ip = o.ip, client_ip = o.client_ip,
             mask = o.mask or link.ip("255.255.255.0")}

  function s:input(dgram)
    local r = dhcp.parse(dgram)
    if not r or r.op ~= 1 or not r.msgtype then return nil end
    local reply
    if r.msgtype == dhcp.DISCOVER then
      reply = dhcp.OFFER
    elseif r.msgtype == dhcp.REQUEST then
      local want = r.opts[50] or r.ciaddr
      reply = (want == self.client_ip or want == ANY) and dhcp.ACK or dhcp.NAK
    else
      return nil
    end
    local body
    if reply == dhcp.NAK then
      body = dhcp.build{op = 2, xid = r.xid, mac = r.mac, msgtype = dhcp.NAK,
                        opts = {{54, self.ip}}}
    else
      body = dhcp.build{op = 2, xid = r.xid, mac = r.mac, msgtype = reply,
                        yiaddr = self.client_ip, siaddr = self.ip,
                        opts = {{54, self.ip}, {51, string.pack(">I4", 86400)},
                                {1, self.mask}, {3, self.ip}, {6, self.ip}}}
    end
    return bcast_frame(self.ip, 67, 68, body)
  end
  return s
end
