-- net.http's server half - the request parser, query decoding and the response
-- builder the config portal needs (#217), split out of http.lua for #219.
--
-- Fetching one file at boot needs an HTTP *client*, not a server, so none of
-- this is on the boot path and none of it is frozen into flash. It extends the
-- existing net.http table, so every caller (iface_x's :serve, setup.lua,
-- ota.lua) keeps working unchanged.
--
-- Load order: after http.lua (it uses the head/body helpers http.lua exports).

net = net or {}
local http = net.http
local feed_head, body_add, body_finish =
  http.feed_head, http.body_add, http.body_finish

-- request parser for the server side: q:feed(...) until q.done, then
-- q.method, q.path, q.query (decoded key=value table), q.headers, q.body.
function http.request()
  local q = {buf = "", headers = {}, chunks = {}, nbody = 0}

  function q:feed(s)
    if not self.method then
      local line = feed_head(self, s)
      if not line then return end
      local m, target = line:match("^(%u+) (%S+)")
      self.method = m or "?"
      local path, qs = (target or "/"):match("^([^?]*)%??(.*)$")
      self.path, self.query = path, http.query(qs)
      s, self.buf = self.buf, nil
    end
    body_add(self, s)
    -- no Content-Length means no body: cap 0 completes a GET immediately
    if self.nbody >= (self.length or 0) then
      body_finish(self, self.length or 0)
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
