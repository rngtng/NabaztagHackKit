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
