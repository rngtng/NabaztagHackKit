-- net.http: builders byte-exact, parsers fed in awkward chunk boundaries.

local http = net.http

eq(http.get("nab.example.org", "/boot/app.lc"),
   "GET /boot/app.lc HTTP/1.0\r\nHost: nab.example.org\r\n"
   .. "Connection: close\r\n\r\n", "get request byte-exact")

-- response with Content-Length, fed byte by byte across every boundary
local RESP = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n"
             .. "Content-Length: 5\r\n\r\nhello EXTRA"
local r = http.response()
for i = 1, #RESP do r:feed(RESP:sub(i, i)) end
eq(r.status, 200, "status parsed")
eq(r.headers["content-type"], "text/plain", "header lowercased")
eq(r.done, true, "done at content-length")
eq(r.body, "hello", "body cut at content-length")

-- no Content-Length: body runs to connection close
local r2 = http.response()
r2:feed("HTTP/1.0 200 OK\r\n\r\npart1 ")
r2:feed("part2")
eq(r2.done, nil, "not done while open")
r2:eof()
eq(r2.done, true, "eof ends read-to-close body")
eq(r2.body, "part1 part2", "read-to-close body")

-- eof before headers is not a success
local r3 = http.response()
r3:feed("HTT")
r3:eof()
eq(r3.done, nil, "eof before headers stays not-done")

eq(http.response_build("200 OK", "hi"),
   "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: 2\r\n"
   .. "Connection: close\r\n\r\nhi", "response builder byte-exact")

-- request parser: GET with an encoded query (the #218 config form)
local q = http.request()
q:feed("GET /save?ssid=My%20AP&psk=a%2Bb+c HTTP/1.1\r\n")
eq(q.done, nil, "request incomplete without blank line")
q:feed("Host: 192.168.0.1\r\n\r\n")
eq(q.done, true, "get done at blank line")
eq(q.method, "GET", "method")
eq(q.path, "/save", "path split from query")
eq(q.query.ssid, "My AP", "percent-decoded value")
eq(q.query.psk, "a+b c", "plus and %2B both decoded")
eq(q.headers["host"], "192.168.0.1", "request header")

-- POST body via content-length, split across feeds
local p = http.request()
p:feed("POST /save HTTP/1.0\r\nContent-Length: 9\r\n\r\nssid=")
eq(p.done, nil, "post waits for full body")
p:feed("rabb")
eq(p.done, true, "post done at content-length")
eq(p.body, "ssid=rabb", "post body")

-- #250: the body must not be reassembled by repeated `body = body .. chunk`.
-- With the GC stopped, heap growth across the feed IS the total allocated, so
-- the quadratic shape (~1.3 MB to assemble 12.5 KB) and the table+concat shape
-- (tens of KB) are unambiguously distinguishable - no timing, no flakiness.
do
  local N, CHUNK = 200, 64
  local piece = ("x"):rep(CHUNK)
  local r = http.response()
  r:feed("HTTP/1.0 200 OK\r\nContent-Length: " .. (N * CHUNK) .. "\r\n\r\n")

  collectgarbage("collect")
  collectgarbage("stop")
  local before = collectgarbage("count")
  for _ = 1, N do r:feed(piece) end
  local grew = collectgarbage("count") - before
  collectgarbage("restart")

  eq(r.done, true, "bulk body completes")
  eq(#r.body, N * CHUNK, "bulk body length")
  eq(r.body, piece:rep(N), "bulk body content")
  ok(grew < 200,
     ("bulk body reassembly allocated %.0f KB for %d KB of body (#250)")
     :format(grew, N * CHUNK // 1024))
end

-- A body containing blank lines: feed_head must split on the FIRST \r\n\r\n
-- only, and everything after it must survive reassembly byte for byte. This is
-- the case a sloppy chunk-table refactor breaks silently.
local r4 = http.response()
r4:feed("HTTP/1.0 200 OK\r\nContent-Length: 14\r\n\r\nab\r\n\r\ncd\r\n\r\nef")
eq(r4.done, true, "body with embedded blank lines completes")
eq(r4.body, "ab\r\n\r\ncd\r\n\r\nef", "embedded blank lines survive intact")

-- Whole response in a single feed (head and body together, one chunk).
local r5 = http.response()
r5:feed("HTTP/1.0 404 Not Found\r\nContent-Length: 3\r\n\r\nnope")
eq(r5.status, 404, "single-feed status")
eq(r5.body, "nop", "single-feed body cut at content-length")

-- Read-to-close body assembled from many feeds, ended by :eof().
local r6 = http.response()
r6:feed("HTTP/1.0 200 OK\r\n\r\n")
for i = 1, 10 do r6:feed(tostring(i % 10)) end
r6:eof()
eq(r6.body, "1234567890", "read-to-close body from many feeds")

-- Same for the request parser's body path.
local p2 = http.request()
p2:feed("POST /x HTTP/1.0\r\nContent-Length: 7\r\n\r\na\r\n\r\nbc")
eq(p2.done, true, "post body with a blank line completes")
eq(p2.body, "a\r\n\r\nbc", "post body with blank line intact")

-- Streaming sink (#265): body bytes are handed over as they arrive and never
-- accumulate, so a file larger than the heap can be played while it downloads.
local got = {}
local r7 = http.response{sink = function(s) got[#got + 1] = s end}
r7:feed("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 9\r\n\r\nabc")
eq(r7.status, 200, "sink mode still parses the head")
eq(r7.headers["content-type"], "audio/mpeg", "and the headers")
eq(table.concat(got), "abc", "first body bytes went to the sink")
ok(not r7.done, "not done at 3 of 9 bytes")
r7:feed("def")
r7:feed("ghiJUNK")
eq(table.concat(got), "abcdefghi", "sink gets the body, capped at Content-Length")
eq(r7.done, true, "done at Content-Length")
eq(r7.body, "", "and the response itself holds no body")
eq(r7.nbody, 9, "but still counts what passed through")

-- read-to-close in sink mode
local got2 = {}
local r8 = http.response{sink = function(s) got2[#got2 + 1] = s end}
r8:feed("HTTP/1.0 200 OK\r\n\r\nstream")
r8:feed("ing")
r8:eof()
eq(table.concat(got2), "streaming", "read-to-close body streamed out")
eq(r8.done, true, "eof completes it")
