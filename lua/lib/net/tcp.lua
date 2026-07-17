-- net.tcp - minimal single-connection TCP (#217): enough for one HTTP
-- exchange in either direction. Stop-and-wait (one segment in flight), fixed
-- advertised window, no congestion control; out-of-order segments are dropped
-- and dup-ACKed, so the peer's retransmit provides ordering. TIME_WAIT is
-- skipped. The caller owns the clock (pass nab.time on device) and pumps
-- :poll() for retransmits; every method returns an array of ready-to-send
-- IPv4 packets (the caller wraps them in SNAP + resolves the MAC - net.iface).
--
-- Sequence arithmetic relies on LUA_32BITS: integers are exactly 32 bits and
-- wrap, so `a - b < 0` is the RFC-style serial compare on both host and rabbit.

net = net or {}
local tcp = {}
net.tcp = tcp
local link, ipv4 = net.link, net.ipv4

tcp.FIN, tcp.SYN, tcp.RST, tcp.PSH, tcp.ACK = 1, 2, 4, 8, 16
local RTO, RETRIES = 3000, 8 -- fixed retransmit timeout (ms), then give up
local MSS = 1200  -- our tx segment cap: 8 SNAP + 20 IP + 20 TCP + 1200 < 1500
local WND = 2920  -- advertised receive window

-- parsed IPv4 packet (proto TCP) -> segment table | nil, err
-- {sport=,dport=,seq=,ack=,flags=,wnd=,payload=[,mss=]}
function tcp.parse(pkt)
  local p = pkt.payload
  if #p < 20 then return nil, "short tcp" end
  local sport, dport, seq, ack, off, flags = string.unpack(">I2I2I4I4BB", p)
  off = (off >> 4) * 4
  if off < 20 or off > #p then return nil, "bad offset" end
  if link.checksum(pkt.src .. pkt.dst
                   .. string.pack(">BBI2", 0, ipv4.TCP, #p) .. p) ~= 0 then
    return nil, "bad checksum"
  end
  local s = {sport = sport, dport = dport, seq = seq, ack = ack,
             flags = flags & 0x3F, wnd = string.unpack(">I2", p, 15),
             payload = p:sub(off + 1)}
  local i = 21 -- options: only MSS (kind 2) matters to us
  while i + 1 <= off do
    local k = p:byte(i)
    if k == 0 then break end
    if k == 1 then
      i = i + 1
    else
      local l = p:byte(i + 1)
      if l < 2 or i + l - 1 > off then break end
      if k == 2 and l == 4 then s.mss = string.unpack(">I2", p, i + 2) end
      i = i + l
    end
  end
  return s
end

local function segment(c, seq, flags, payload, opts)
  opts = opts or ""
  local hdr = string.pack(">I2I2I4I4BBI2I2I2", c.sport, c.dport, seq,
                          (flags & tcp.ACK) ~= 0 and c.rcv_nxt or 0,
                          (5 + #opts // 4) << 4, flags, WND, 0, 0) .. opts
  local seg = hdr .. payload
  local ck = link.checksum(c.src .. c.dst
                           .. string.pack(">BBI2", 0, ipv4.TCP, #seg) .. seg)
  return ipv4.build{src = c.src, dst = c.dst, proto = ipv4.TCP,
                    payload = seg:sub(1, 16) .. string.pack(">I2", ck)
                              .. seg:sub(19)}
end

-- arm the (single) retransmit slot and emit its segment
local function arm(c, out, flags, payload, opts)
  c.rtx = {seq = c.snd_nxt, flags = flags, payload = payload, opts = opts,
           time = c.clock(), tries = 0,
           len = #payload + ((flags & (tcp.SYN | tcp.FIN)) ~= 0 and 1 or 0)}
  c.snd_nxt = c.snd_nxt + c.rtx.len
  out[#out + 1] = segment(c, c.rtx.seq, flags, payload, opts)
end

-- send queued data / a pending FIN once the previous segment is acked
local function flush(c, out)
  if not c.rtx and #c.txq > 0
     and (c.state == "established" or c.state == "close-wait") then
    local chunk = c.txq:sub(1, c.mss)
    c.txq = c.txq:sub(#chunk + 1)
    arm(c, out, tcp.PSH | tcp.ACK, chunk)
  end
  if not c.rtx and c.finq and #c.txq == 0 then
    if c.state == "established" then
      c.state = "fin-wait-1"
    elseif c.state == "close-wait" then
      c.state = "last-ack"
    else
      return
    end
    c.finq = nil
    arm(c, out, tcp.FIN | tcp.ACK, "")
  end
end

local function new(o)
  local c = {src = o.src, dst = o.dst, sport = o.sport, dport = o.dport,
             clock = o.clock or (nab and nab.time) or function() return 0 end,
             iss = o.iss, state = "closed", txq = "", rxq = "", mss = 536}

  function c:connect()
    local out = {}
    self.iss = self.iss or (self.clock() * 31 + 7) & 0x7FFFFFFF
    self.snd_nxt = self.iss
    self.state = "syn-sent"
    arm(self, out, tcp.SYN, "", string.pack(">BBI2", 2, 4, 1460))
    return out
  end

  -- queue data (chunked to the peer's MSS, one segment in flight)
  function c:send(data)
    local out = {}
    self.txq = self.txq .. data
    flush(self, out)
    return out
  end

  -- drain everything received so far
  function c:read()
    local d = self.rxq
    self.rxq = ""
    return d
  end

  function c:close()
    local out = {}
    self.finq = true
    flush(self, out)
    return out
  end

  -- retransmit timer; call regularly with the current ms clock
  function c:poll(now)
    local out = {}
    now = now or self.clock()
    if self.rtx and now - self.rtx.time >= RTO then
      self.rtx.tries = self.rtx.tries + 1
      if self.rtx.tries > RETRIES then
        self.state, self.err, self.rtx = "closed", "timeout", nil
        return out
      end
      self.rtx.time = now
      out[#out + 1] = segment(self, self.rtx.seq, self.rtx.flags,
                              self.rtx.payload, self.rtx.opts)
    end
    flush(self, out)
    return out
  end

  function c:input(pkt)
    local out = {}
    local s = tcp.parse(pkt)
    if not s or s.dport ~= self.sport then return out end
    if self.dport and s.sport ~= self.dport then return out end
    if self.dst and pkt.src ~= self.dst then return out end
    if self.state == "closed" then return out end

    if (s.flags & tcp.RST) ~= 0 then
      if self.state ~= "listen" then
        self.state, self.err, self.rtx = "closed", "reset", nil
      end
      return out
    end

    if self.state == "listen" then
      if (s.flags & tcp.SYN) ~= 0 and (s.flags & tcp.ACK) == 0 then
        self.dst, self.dport = pkt.src, s.sport
        self.rcv_nxt = s.seq + 1
        if s.mss and s.mss < MSS then self.mss = s.mss else self.mss = MSS end
        self.iss = self.iss or (self.clock() * 31 + 5) & 0x7FFFFFFF
        self.snd_nxt = self.iss
        self.state = "syn-received"
        arm(self, out, tcp.SYN | tcp.ACK, "", string.pack(">BBI2", 2, 4, 1460))
      end
      return out
    end

    if self.state == "syn-sent" then
      if (s.flags & (tcp.SYN | tcp.ACK)) == tcp.SYN | tcp.ACK
         and s.ack == self.iss + 1 then
        self.rcv_nxt = s.seq + 1
        if s.mss and s.mss < MSS then self.mss = s.mss else self.mss = MSS end
        self.rtx = nil
        self.state = "established"
        out[#out + 1] = segment(self, self.snd_nxt, tcp.ACK, "")
        flush(self, out)
      end
      return out
    end

    -- established and closing states ------------------------------------

    -- our in-flight segment is acked (stop-and-wait: all-or-nothing)
    if (s.flags & tcp.ACK) ~= 0 and self.rtx
       and s.ack == self.rtx.seq + self.rtx.len then
      local was_fin = (self.rtx.flags & tcp.FIN) ~= 0
      local was_syn = (self.rtx.flags & tcp.SYN) ~= 0
      self.rtx = nil
      if was_syn then -- our SYN-ACK: handshake complete
        self.state = "established"
      elseif was_fin then
        if self.state == "fin-wait-1" then
          self.state = "fin-wait-2"
        elseif self.state == "closing" or self.state == "last-ack" then
          self.state = "closed"
        end
      end
    end

    -- in-order data / FIN; anything else with content gets a dup-ACK
    local fin = (s.flags & tcp.FIN) ~= 0
    if s.seq == self.rcv_nxt and (#s.payload > 0 or fin) then
      self.rxq = self.rxq .. s.payload
      self.rcv_nxt = self.rcv_nxt + #s.payload
      if fin then
        self.rcv_nxt = self.rcv_nxt + 1
        if self.state == "established" then
          self.state = "close-wait"
        elseif self.state == "fin-wait-1" then
          self.state = self.rtx and "closing" or "closed"
        elseif self.state == "fin-wait-2" then
          self.state = "closed"
        end
      end
      out[#out + 1] = segment(self, self.snd_nxt, tcp.ACK, "")
    elseif #s.payload > 0 or fin then
      out[#out + 1] = segment(self, self.snd_nxt, tcp.ACK, "") -- dup-ACK
    end

    flush(self, out)
    return out
  end

  return c
end

-- {src=,dst=,sport=,dport=[,iss=,clock=]} -> conn; open with :connect()
function tcp.client(o)
  return new(o)
end

-- {src=,port=[,iss=,clock=]} -> conn in "listen"; accepts the first SYN
function tcp.listen(o)
  local c = new{src = o.src, sport = o.port, iss = o.iss, clock = o.clock}
  c.state = "listen"
  return c
end
