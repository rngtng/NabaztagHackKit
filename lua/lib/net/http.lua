-- net.http - GET client + one-page server pieces (#217).
--
-- Transport-free: builders return byte strings for tcp:send, parsers are fed
-- from tcp:read in whatever chunks arrive. HTTP/1.0, Connection: close - the
-- body ends at Content-Length when given, else at connection close (:eof).
-- No chunked encoding: point the boot URL at a plain file.

net = net or {}
local http = {}
net.http = http

function http.get(host, path)
  return "GET " .. path .. " HTTP/1.0\r\nHost: " .. host
         .. "\r\nConnection: close\r\n\r\n"
end

-- shared incremental head parser: fills o.headers (lowercased names) and
-- returns the first line once the blank line has arrived
local function feed_head(o, s)
  o.buf = o.buf .. s
  local head, rest = o.buf:match("^(.-)\r\n\r\n(.*)$")
  if not head then return nil end
  o.buf = rest
  local first
  for line in (head .. "\r\n"):gmatch("(.-)\r\n") do
    if not first then
      first = line
    else
      local k, v = line:match("^([^:]+):%s*(.-)%s*$")
      if k then o.headers[k:lower()] = v end
    end
  end
  local cl = o.headers["content-length"]
  if cl then o.length = tonumber(cl) end
  return first
end

-- response parser: r:feed(tcp:read()) until r.done (or tcp closes -> r:eof()).
-- Then r.status (numeric), r.headers, r.body.
function http.response()
  local r = {buf = "", headers = {}}

  function r:feed(s)
    if not self.status then
      local line = feed_head(self, s)
      if not line then return end
      self.status = tonumber(line:match("^HTTP/%d%.%d (%d+)")) or 0
      s, self.buf, self.body = self.buf, nil, ""
    end
    self.body = self.body .. s
    if self.length and #self.body >= self.length then
      self.body = self.body:sub(1, self.length)
      self.done = true
    end
  end

  function r:eof() -- connection closed: without Content-Length that's the end
    if self.status and not self.length then self.done = true end
  end

  return r
end

-- request parser for the server side: q:feed(...) until q.done, then
-- q.method, q.path, q.query (decoded key=value table), q.headers, q.body.
function http.request()
  local q = {buf = "", headers = {}}

  function q:feed(s)
    if not self.method then
      local line = feed_head(self, s)
      if not line then return end
      local m, target = line:match("^(%u+) (%S+)")
      self.method = m or "?"
      local path, qs = (target or "/"):match("^([^?]*)%??(.*)$")
      self.path, self.query = path, http.query(qs)
      s, self.buf, self.body = self.buf, nil, ""
    end
    self.body = self.body .. s
    if #self.body >= (self.length or 0) then
      self.body = self.body:sub(1, self.length or 0)
      self.done = true
    end
  end

  return q
end

-- "a=1&b=hello%20world" -> {a="1", b="hello world"} (+ form-style '+')
function http.query(qs)
  local t = {}
  for k, v in (qs or ""):gmatch("([^&=]+)=([^&]*)") do
    t[k] = v:gsub("%+", " "):gsub("%%(%x%x)",
      function(h) return string.char(tonumber(h, 16)) end)
  end
  return t
end

function http.response_build(status, body, ctype)
  return ("HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
          .. "Connection: close\r\n\r\n"):format(status, ctype or "text/html",
                                                 #body) .. body
end
