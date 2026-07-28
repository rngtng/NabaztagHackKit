-- audio.stream: an HTTP body arriving in TCP-sized dribbles, and what the
-- decoder ends up with. Drives net.http's real parser (loaded from the sibling
-- lib) against a fake connection + interface, so the head parsing, the
-- prebuffer, the high-water flow control and the end-of-body rules are the
-- ones the rabbit runs.

RUNFILE(LIBDIR .. "/net/http.lua")

local stream = audio.stream

-- fake net.tcp connection: hands over pre-canned reads, one per :read()
local function fakeconn(reads)
  local i = 0
  return {state = "established", closed = 0, reads = reads,
          read = function(self)
            i = i + 1
            return self.reads[i] or ""
          end,
          close = function(self) self.closed = self.closed + 1; return {} end}
end

-- fake net.iface: counts polls so the flow control is observable
local function fakeifc(conn)
  return {conn = conn, polls = 0, time = function() return 0 end,
          poll = function(self) self.polls = self.polls + 1 end,
          pump = function() end}
end

local HEAD = "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
             .. "Content-Length: 12\r\n\r\n"

-- head parsing + body pass-through -------------------------------------------

local conn = fakeconn{HEAD .. "abcd", "efgh", "ijkl"}
local ifc = fakeifc(conn)
local s = stream.source(ifc, conn, {prebuffer = 1})

eq(s:pull(), "", "nothing to pull before the first poll")
s:poll()
eq(s.status, 200, "status parsed off the head")
eq(s.parser.headers["content-type"], "audio/mpeg", "headers parsed")
eq(s:pull(), "abcd", "the body starts after the blank line, head stripped")
eq(s:pull(), "", "empty until more arrives")
s:poll(); s:poll()
eq(s:pull(), "efgh", "chunks come out in arrival order")
eq(s:pull(), "ijkl", "and the last one")
ok(s.eof, "Content-Length satisfied -> end of stream")
eq(s:pull(), nil, "nil says the source is done")

-- Content-Length caps the body: a chatty server's extra bytes are not audio

conn = fakeconn{HEAD .. "0123456789ABCDEF"}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1})
s:poll()
eq(s:pull(), "0123456789AB", "trimmed to Content-Length, not 16 bytes")
eq(s:pull(), nil, "then done")

-- prebuffer: the player is told "nothing yet" until the buffer fills ---------

conn = fakeconn{HEAD:gsub("Content%-Length: 12\r\n", "") .. "aaaa",
                "bbbb", "cccc"}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 10})
s:poll()
eq(s:pull(), "", "4 buffered bytes is below the prebuffer")
s:poll()
eq(s:pull(), "", "8 is still below it")
s:poll()
eq(s:pull(), "aaaa", "12 bytes in hand -> playback may start")
eq(s:pull(), "bbbb", "and keeps flowing without re-prebuffering")

-- flow control: no reads while the buffer is above the high-water mark -------

conn = fakeconn{HEAD:gsub("Content%-Length: 12\r\n", "") .. string.rep("x", 40),
                string.rep("y", 40)}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1, high = 32})
s:poll()
eq(s.n, 40, "first read buffered 40 bytes")
eq(ifc.polls, 1, "one interface poll so far")
s:poll()
eq(ifc.polls, 1, "above the high-water mark the socket is left alone")
eq(s.n, 40, "so nothing more was buffered")
eq(s:pull(), string.rep("x", 40), "drain it")
s:poll()
eq(ifc.polls, 2, "with room again the reads resume")
eq(s.n, 40, "and the next 40 bytes arrived")

-- read-to-close (no Content-Length) ------------------------------------------

conn = fakeconn{"HTTP/1.0 200 OK\r\n\r\nsong", "more"}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1})
s:poll(); s:poll()
eq(s.n, 8, "both chunks buffered")
ok(not s.eof, "still open")
conn.state = "close-wait"
s:poll()
ok(s.eof, "the close ends the body")
eq(s:pull(), "song", "buffered bytes survive the close")
eq(s:pull(), "more", "all of them")
eq(s:pull(), nil, "then end of stream")

-- a non-200 response is not audio --------------------------------------------

conn = fakeconn{"HTTP/1.0 404 Not Found\r\nContent-Length: 3\r\n\r\nno!"}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1})
s:poll()
eq(s.status, 404, "status recorded")
eq(s.err, "http 404", "reported as an error")
ok(s.eof, "and the source ends")
eq(s:pull(), nil, "nothing is played")

-- a dead connection ends the source ------------------------------------------

conn = fakeconn{}
conn.err = "timeout"
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1})
s:poll()
eq(s.err, "timeout", "the connection's error propagates")
eq(s:pull(), nil, "and the source is done")

-- close() tears the connection down ------------------------------------------

conn = fakeconn{HEAD .. "abcdefghijkl"}
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 1})
s:poll()
s:close()
eq(conn.closed, 1, "connection closed once")
eq(ifc.conn, nil, "and released from the interface")
eq(s:pull(), nil, "buffered audio dropped")

-- end to end: player + stream, straight to the decoder -----------------------

local BODY = string.rep("MP3 FRAME 0123456789", 30)   -- 600 bytes
local reads = {"HTTP/1.0 200 OK\r\nContent-Length: " .. #BODY .. "\r\n\r\n"}
for i = 1, #BODY, 50 do reads[#reads + 1] = BODY:sub(i, i + 49) end
conn = fakeconn(reads)
ifc = fakeifc(conn)
s = stream.source(ifc, conn, {prebuffer = 100})

local d = FAKEDEC{rate = 64}
local p = audio.player.new(d.drv)
p:play(s)
local st
for _ = 1, 2000 do
  st = p:step()
  if st == "idle" or st == "error" then break end
end
eq(st, "idle", "the stream played to completion")
eq(p.err, nil, "no error")
eq(d.nfed, #BODY + 2048, "the whole body plus the tail reached the decoder")
eq(d:bytes():sub(1, #BODY), BODY, "byte for byte, in order")
eq(conn.closed, 1, "and the connection was closed by the player")
