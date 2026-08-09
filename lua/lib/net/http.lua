-- net.http - the GET client (#217).
--
-- Transport-free: builders return byte strings for tcp:send, parsers are fed
-- from tcp:read in whatever chunks arrive. HTTP/1.0, Connection: close - the
-- body ends at Content-Length when given, else at connection close (:eof).
-- No chunked encoding: point the boot URL at a plain file.
--
-- The **server** half - http.request, http.query, http.response_build, which
-- only the config portal uses - is httpd.lua. It is off the #219 boot path
-- (fetching one file needs a client, not a server), so it is not frozen into
-- flash. The three head/body helpers below are shared with it, and exported
-- for that reason alone.

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

-- Body chunks accumulate in a table and are joined exactly once (#250).
-- `body = body .. chunk` per arriving segment copies and discards the whole
-- body every time - O(n^2) bytes, all of it churned through the device's 1 MB
-- heap. `nbody` carries the running length so the completion test never needs
-- an intermediate join. `cap` is the Content-Length to truncate at, or nil to
-- keep everything (read-to-close).
--
-- A `sink` (see http.response) takes the body bytes instead: the response then
-- holds no body at all, which is what streaming audio (#265) needs - the file
-- is bigger than the heap. `nbody` still counts, so Content-Length completion
-- works the same.
local function body_add(o, s)
  if s == "" then return end
  if o.sink then
    if o.length then                    -- never hand the sink past the cap
      local room = o.length - o.nbody
      if room <= 0 then return end
      if #s > room then s = s:sub(1, room) end
    end
    o.nbody = o.nbody + #s
    o.sink(s)
  else
    o.chunks[#o.chunks + 1] = s
    o.nbody = o.nbody + #s
  end
end

local function body_finish(o, cap)
  o.body = table.concat(o.chunks)
  if cap then o.body = o.body:sub(1, cap) end
  o.done = true
end

-- The three above are httpd.lua's too: a request and a response differ only in
-- their first line, and duplicating a head parser to avoid three table fields
-- would be the worse trade. Not part of the module's public surface.
http.feed_head, http.body_add, http.body_finish = feed_head, body_add, body_finish

-- response parser: r:feed(tcp:read()) until r.done (or tcp closes -> r:eof()).
-- Then r.status (numeric), r.headers, r.body.
--
-- o.sink = fn(chunk): stream mode - body bytes are handed over as they arrive
-- and r.body stays empty (audio.stream, #265). r.nbody still counts them.
function http.response(o)
  local r = {buf = "", headers = {}, chunks = {}, nbody = 0,
             sink = o and o.sink}

  function r:feed(s)
    if not self.status then
      local line = feed_head(self, s)
      if not line then return end
      self.status = tonumber(line:match("^HTTP/%d%.%d (%d+)")) or 0
      s, self.buf = self.buf, nil
    end
    body_add(self, s)
    if self.length and self.nbody >= self.length then
      body_finish(self, self.length)
    end
  end

  function r:eof() -- connection closed: without Content-Length that's the end
    if self.status and not self.length then body_finish(self, nil) end
  end

  return r
end

