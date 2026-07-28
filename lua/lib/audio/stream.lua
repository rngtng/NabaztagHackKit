-- audio.stream - an HTTP body as an audio.player source (#265): the rabbit
-- plays a file off the network instead of out of its heap, which is what
-- mtl/lib/audio/audiolib.mtl did on the V1 stack (`audiolib_start_http`).
--
-- Flow control runs on the *decoder's* side, exactly as in mtl: the player
-- feeds only what the VS1003 accepts, so a full FIFO leaves bytes in this
-- source's queue; above HIGH bytes buffered we stop reading the socket
-- altogether. Our advertised TCP window is fixed (net/tcp.lua), so "stop
-- reading" means we stop polling the interface - the peer's in-flight segment
-- goes unacknowledged and its retransmit timer paces it down. That is mtl's
-- `http_enable 0` with the same effect and none of the machinery.
--
-- Playback does not start on the first byte: PREBUFFER bytes must be in hand
-- (or the body must have ended), so a slow first window does not produce a
-- gap - `pull()` returns "" until then and the player just idles.
--
--   local ifc = net.iface.new(net.iface.nabdrv())     -- wifi already up
--   local s = audio.stream.http(ifc, net.link.ip("192.168.0.10"), "srv",
--                               "/song.mp3")
--   p:play(s); while p:busy() do p:step() end
--
-- Only one connection exists at a time (net.iface tracks a single `conn`), so
-- streaming and serving cannot overlap until #262 lands - this is the single
-- stream the issue scopes.

audio = audio or {}
local stream = {}
audio.stream = stream

stream.HIGH = 24576      -- stop reading the socket above this many bytes queued
stream.PREBUFFER = 8192  -- bytes in hand before the first byte is fed

-- Wrap an already-connected net.tcp connection as a player source. `ifc` is
-- the net.iface driving it (polled from :poll()), `conn` anything with
-- :read() -> string, :close() -> packets and a .state field.
-- o = {high=, prebuffer=, ok=fn(status)->bool}
function stream.source(ifc, conn, o)
  o = o or {}
  local s = {ifc = ifc, conn = conn, q = {}, n = 0, eof = false, err = nil,
             started = false, status = nil,
             high = o.high or stream.HIGH,
             prebuffer = o.prebuffer or stream.PREBUFFER}

  -- Body bytes go straight into our queue instead of net.http's accumulator:
  -- a stream must never hold the whole file (that is the point).
  s.parser = net.http.response{sink = function(chunk)
    s.q[#s.q + 1] = chunk
    s.n = s.n + #chunk
  end}

  -- End of body. With an error, whatever is buffered is not audio (an error
  -- page, a truncated file) - drop it rather than feed it to the decoder.
  local function ended(self, err)
    if err and not self.err then
      self.err = err
      self.q, self.n = {}, 0
    end
    self.eof = true
  end

  -- one bounded slice of network work; called by the player every step
  function s:poll()
    if self.eof then return end
    if self.n < self.high then          -- flow control: full buffer, no reads
      self.ifc:poll(0)
      local d = self.conn:read()
      if d ~= "" then self.parser:feed(d) end
      if self.status == nil and self.parser.status then
        self.status = self.parser.status
        local ok = o.ok and o.ok(self.status) or self.status == 200
        if not ok then ended(self, "http " .. self.status) end
      end
    end
    if self.conn.err then
      ended(self, self.conn.err)
    elseif self.conn.state == "close-wait" or self.conn.state == "closed" then
      self.parser:eof()                 -- no Content-Length: close is the end
      ended(self)
    elseif self.parser.done then
      ended(self)
    end
  end

  -- next chunk for the decoder: "" = nothing to feed yet, nil = end of stream
  function s:pull()
    if self.n == 0 then
      if self.eof then return nil end
      return ""
    end
    if not self.started then
      if self.n < self.prebuffer and not self.eof then return "" end
      self.started = true
    end
    local c = table.remove(self.q, 1)
    self.n = self.n - #c
    return c
  end

  -- the player calls this when the source is done or playback is stopped
  function s:close()
    if self.conn.state ~= "closed" then
      self.ifc:pump(self.conn, self.conn:close())   -- best-effort teardown
    end
    if self.ifc.conn == self.conn then self.ifc.conn = nil end
    self.q, self.n, self.eof = {}, 0, true
  end

  return s
end

-- Open GET dst_ip:port path and return the source. DNS is out of scope (as in
-- net.iface:http_get), so the address is explicit and `host` only fills the
-- Host header.
function stream.http(ifc, dst_ip, host, path, o)
  o = o or {}
  local c = net.tcp.client{src = ifc.ip, dst = dst_ip, dport = o.port or 80,
                           sport = 49152 + (ifc.time() & 0x3FFF),
                           clock = ifc.time}
  ifc.conn = c
  ifc:pump(c, c:connect())
  ifc:pump(c, c:send(net.http.get(host, path)))   -- queued until established
  return stream.source(ifc, c, o)
end
