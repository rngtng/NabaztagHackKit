-- audio.player: what the *decoder* received, chunk by chunk, under short
-- feeds - plus queueing, underrun, drain, stall and stop.

local player = audio.player

-- pump at most n steps, stopping when the player goes idle; -> last state
local function run(p, n)
  local st
  for _ = 1, n or 10000 do
    st = p:step()
    if st == "idle" or st == "error" then return st end
  end
  return st
end

-- a source that yields the given chunks, with "" (nothing yet) where the
-- table holds `false`, and nil after the last one
local function chunks(list)
  local i = 0
  return {pull = function()
    i = i + 1
    local c = list[i]
    if c == nil then return nil end
    if c == false then return "" end
    return c
  end}
end

-- a string source plays through, then flushes the tail ----------------------

local BODY = "ID3\4\0\0\0\0\0\0" .. string.rep("mp3 payload ", 20)
local d = FAKEDEC{rate = 32}
local p = player.new(d.drv)
p:play(BODY)
ok(p:busy(), "busy once a source is queued")
eq(run(p, 500), "idle", "plays to idle")

eq(d.started, 1, "opened the stream once")
eq(d.stopped, 1, "closed the stream once")
eq(d.nfed, #BODY + 2048, "fed the body plus a 2048-byte tail")
eq(d:bytes():sub(1, #BODY), BODY, "the decoder got the body verbatim")
eq(d:bytes():sub(#BODY + 1), string.rep("\0", 2048), "tail is zero endFillBytes")
eq(#d.fed[1], 32, "feeds are capped by what the decoder accepts")
ok(not p:busy(), "idle when done")
eq(p.err, nil, "no error")

-- a decoder that only takes 1 byte per turn still gets everything ------------

d = FAKEDEC{rate = 1}
p = player.new(d.drv)
p:play("abc")
eq(run(p, 5000), "idle", "one-byte-per-turn decoder finishes")
eq(d:bytes(), "abc" .. string.rep("\0", 2048), "same bytes, 2051 feeds")
eq(#d.fed, 2051, "one feed per accepted byte")

-- queue: two sources play back to back, in order -----------------------------

d = FAKEDEC{rate = 64}
p = player.new(d.drv)
p:play("first"):queue("second")
eq(run(p, 500), "idle", "queue drains to idle")
eq(d.started, 2, "each source opens its own stream")
eq(d.nfed, #"first" + #"second" + 2 * 2048, "both bodies, both tails")
local seen = d:bytes():gsub("%z", "")
eq(seen, "firstsecond", "played in queue order")

-- underrun: an empty pull idles the player, it never feeds silence -----------

d = FAKEDEC{rate = 64}
p = player.new(d.drv)
p:play(chunks{"aaa", false, false, "bbb"})
eq(p:step(), "playing", "first chunk feeds")
eq(d.nfed, 3, "3 bytes so far")
eq(p:step(), "buffering", "empty pull -> buffering")
eq(p:step(), "buffering", "still buffering")
eq(d.nfed, 3, "nothing was fed while buffering")
eq(run(p, 500), "idle", "recovers when the source produces again")
eq(d:bytes():gsub("%z", ""), "aaabbb", "both chunks reached the decoder")

-- drain: the player waits for the decoder to fall quiet ----------------------

d = FAKEDEC{rate = 4096, drain = 2}
p = player.new(d.drv)
p:play("x")
eq(p:step(), "playing", "feeds the body")
eq(p:step(), "playing", "feeds the tail")
eq(p:step(), "draining", "then waits for the decoder")
eq(d.stopped, 0, "stream still open while draining")
eq(p:step(), "draining", "still draining")
eq(p:step(), "idle", "idle once busy() clears")
eq(d.stopped, 1, "stream closed exactly once")

-- a decoder that never drains is capped by DRAIN ms --------------------------

d = FAKEDEC{rate = 4096, drain = 1000000}
p = player.new(d.drv)
p:play("x")
p:step(); p:step()
eq(p:step(), "draining", "draining")
d.t = player.DRAIN + 1
eq(p:step(), "idle", "gives up on the drain wait after DRAIN ms")

-- stall: a decoder that stops accepting fails, it does not spin forever ------

d = FAKEDEC{rate = 8}
p = player.new(d.drv)
p:play(string.rep("z", 4096))
p:step()
d.rate = 0                      -- DREQ never comes back
eq(p:step(), "playing", "one no-progress step is fine")
d.t = player.STALL + 1
eq(p:step(), "error", "stalls out after STALL ms")
eq(p.err, "decoder stalled", "reports why")
eq(d.stopped, 1, "and closes the stream")
ok(not p:busy(), "not busy after a stall")

-- a source that never delivers stalls too ------------------------------------

d = FAKEDEC{rate = 64}
p = player.new(d.drv)
p:play(chunks{false, false, false, false})
p:step()
d.t = player.STALL + 1
eq(p:step(), "error", "source stall detected")
eq(p.err, "source stalled", "reports the source, not the decoder")

-- a frozen clock (the simulator) still terminates ----------------------------

d = FAKEDEC{rate = 0}
p = player.new(d.drv)
local saved = player.STALL_STEPS
player.STALL_STEPS = 20         -- the device value is 50000; same code path
p:play("nope")
eq(run(p, 500), "error", "step-counted stall fires with a still clock")
player.STALL_STEPS = saved

-- stop() cuts playback and drops the queue -----------------------------------

d = FAKEDEC{rate = 4}
p = player.new(d.drv)
p:play("aaaaaaaaaaaa"):queue("bbbb")
p:step()
eq(d.nfed, 4, "one feed happened")
p:stop()
ok(not p:busy(), "stop clears the queue")
eq(d.stopped, 1, "and closes the stream")
eq(run(p, 10), "idle", "stays idle")
eq(d.nfed, 4, "nothing more reached the decoder")

-- close(): the player tells a source when it is done -------------------------

local closed = 0
local src = chunks{"q"}
src.close = function() closed = closed + 1 end
d = FAKEDEC{rate = 4096}
p = player.new(d.drv)
p:play(src)
run(p, 100)
eq(closed, 1, "source closed when it finished")

closed = 0
src = chunks{"q", false, false}
src.close = function() closed = closed + 1 end
p = player.new(FAKEDEC{rate = 4096}.drv)
p:play(src)
p:step()
p:stop()
eq(closed, 1, "source closed when playback was stopped")
