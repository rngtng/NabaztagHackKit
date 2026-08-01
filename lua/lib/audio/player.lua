-- audio.player - the non-blocking playback engine (#265), ported from
-- mtl/lib/audio/audiolib.mtl's playStart/playFeed/playStop core onto the
-- lua-track HAL (`nab.play_start`/`play_feed`/`playing`/`play_stop`).
--
-- The point of the exercise: `nab.play(data)` blocks until the last byte is
-- decoded, so the rabbit could not make a sound and do anything else. Here the
-- decoder is fed a burst at a time from the cooperative loop - LEDs animate,
-- the net gets polled and the REPL stays responsive between feeds - and a
-- source can be longer than the heap, because it is pulled in chunks.
--
-- Pull-style like lib/net and lib/hw/ears: the whole HAL is injected, the
-- caller owns the clock and pumps :step(); :wait() is the blocking convenience.
--
--   local p = audio.player.new(audio.nabdrv())
--   p:play(nab.tone())                     -- a string source
--   while p:busy() do p:step() end         -- ... or p:wait()
--   p:play(audio.stream.http(ifc, ip, "srv", "/song.mp3"))   -- a pulled source
--
-- A **source** is either a byte string, or an object with:
--   :pull() -> chunk | "" (nothing yet - the player idles, it does NOT feed
--             silence) | nil (end of stream)
--   :poll() (optional) - called once per :step() so a source can do its own
--             bounded work (a stream reads its socket here)
--   :close() (optional) - called when the source is done or playback is stopped
--
-- End of a source is >=2048 zero endFillBytes (TAIL) pushed through the same
-- feed - the VS10xx flush the datasheet asks for, and what mtl's audiolib
-- appended to its FIFO too - followed by waiting for `busy()` (SCI_HDAT1) to
-- fall, which is what "the sound actually finished" means.

audio = audio or {}
local player = {}
audio.player = player

-- endFillByte tail, built once (2 KB of heap, not flash)
player.TAIL = string.rep("\0", 2048)

player.DRAIN = 3000   -- ms cap on waiting for the decoder to fall quiet
player.STALL = 15000  -- ms with no byte accepted and no data -> give up
-- Same give-up rule counted in steps, for a clock that does not move: the
-- simulator has no DREQ model (every feed accepts 0) and a `nab.time` that can
-- sit still, so a purely time-based bound would spin forever there. On the
-- device this is >1 s of pumping with not one byte accepted.
player.STALL_STEPS = 50000

-- the real hardware
function audio.nabdrv()
  return {start = nab.play_start, feed = nab.play_feed, stop = nab.play_stop,
          busy = nab.playing, time = nab.time, sleep = nab.wait}
end

-- nab.time() is a wrapping 32-bit tick (#259): differences are taken modulo
-- its width, never as plain subtraction (same rule as lib/hw/ears).
local function elapsed(now, t0)
  return (now - t0) & 0xFFFFFFFF
end

-- a plain string is a source that yields itself once
local function as_source(s)
  if type(s) ~= "string" then return s end
  local sent = false
  return {pull = function()
    if sent then return nil end
    sent = true
    return s
  end}
end

-- drv = {start=fn(), feed=fn(s, i)->accepted, stop=fn(), busy=fn()->bool,
--        time=fn()->ms, sleep=fn(ms)}  -- sleep only used by :wait()
function player.new(drv)
  local p = {drv = drv, q = {}, src = nil, buf = "", pos = 1, state = "idle",
             err = nil, idle_steps = 0, opened = false}

  -- no byte moved this step: true once we have waited long enough (either
  -- clock) to call it a stall
  local function stalled(self, now)
    self.idle_steps = self.idle_steps + 1
    return elapsed(now, self.tprog) > player.STALL
           or self.idle_steps > player.STALL_STEPS
  end

  local function close_src(self)
    local s = self.src
    self.src = nil
    if s and s.close then s:close() end
  end

  -- close the decoder stream, but only one we actually opened: play()/stop()
  -- on an idle player must not touch the codec (or the amplifier) at all
  local function close_stream(self)
    if self.opened then
      self.opened = false
      self.drv.stop()
    end
  end

  -- current source finished (tail drained): on to the next one, if any
  local function finish(self)
    close_src(self)
    self.buf, self.pos, self.state = "", 1, "idle"
    close_stream(self)
  end

  local function fail(self, err)
    self.err = err
    self:stop()
    return "error"
  end

  -- queue a source (string or pull object); playback starts on the next :step()
  function p:queue(src)
    self.q[#self.q + 1] = src
    return self
  end

  -- stop whatever is playing and play this source instead
  function p:play(src)
    self:stop()
    self.err = nil
    return self:queue(src)
  end

  -- cut playback and drop the queue; the decoder keeps whatever is in its FIFO
  function p:stop()
    close_src(self)
    self.q, self.buf, self.pos, self.state = {}, "", 1, "idle"
    close_stream(self)
    return self
  end

  -- something to do? (playing, draining, or still queued)
  function p:busy()
    return self.state ~= "idle" or #self.q > 0
  end

  -- One turn of the engine. Returns the state it left the player in:
  -- "idle" | "playing" | "buffering" (source has no bytes yet) | "draining"
  -- (tail fed, decoder emptying) | "error" (see .err). Never blocks.
  function p:step()
    local drv = self.drv
    local now = drv.time()

    if self.state == "idle" then
      if #self.q == 0 then return "idle" end
      self.src = as_source(table.remove(self.q, 1))
      self.buf, self.pos, self.state, self.tprog = "", 1, "play", now
      self.idle_steps = 0
      self.opened = true
      drv.start()
    end

    local src = self.src
    if src.poll then src:poll() end   -- a stream refills its own buffer here

    if self.pos > #self.buf then      -- current chunk fully fed: what next?
      if self.state == "play" then
        local chunk = src:pull()
        if chunk == nil then                       -- source ended: flush tail
          self.buf, self.pos, self.state = player.TAIL, 1, "tail"
        elseif chunk == "" then                    -- nothing yet: underrun
          if stalled(self, now) then return fail(self, "source stalled") end
          return "buffering"
        else
          self.buf, self.pos, self.tprog = chunk, 1, now
          self.idle_steps = 0
        end
      elseif self.state == "tail" then             -- tail fully fed
        self.state, self.tdrain = "drain", now
      end
    end

    if self.state == "drain" then
      -- HDAT1 falls when the decoder has chewed through the tail; the cap
      -- covers a codec that never reports idle (and the simulator, where the
      -- SCI read always says 0 - there it just ends immediately).
      if not drv.busy() or elapsed(now, self.tdrain) > player.DRAIN then
        finish(self)
        return self:busy() and "playing" or "idle"
      end
      return "draining"
    end

    local n = drv.feed(self.buf, self.pos)
    if n > 0 then
      self.pos, self.tprog, self.idle_steps = self.pos + n, now, 0
    elseif stalled(self, now) then
      return fail(self, "decoder stalled")   -- DREQ never came back
    end
    return "playing"
  end

  -- blocking convenience for the REPL: pump until the queue is done (or ms
  -- elapse). Everything :step() drives still runs; nothing else does.
  function p:wait(ms)
    local t0 = self.drv.time()
    while self:busy() do
      self:step()
      if ms and elapsed(self.drv.time(), t0) > ms then return false end
      if self.drv.sleep then self.drv.sleep(0) end
    end
    return self.err == nil
  end

  -- Hand :step() to the cooperative reactor (#283), so the decoder keeps being
  -- fed from every nab.wait/nab.delay and from the REPL's idle loop - not only
  -- from a loop the app remembers to write. This is what makes "play a sound
  -- AND do something else" hold even when the something else is itself a
  -- blocking call: without it, an HTTP GET or an ear move starves the feed and
  -- the decoder underruns mid-clip.
  --
  -- Guarded because `sched` is a device global: the host unit tests drive a
  -- fake decoder with no reactor in sight, and pumping :step() from a loop
  -- stays perfectly valid there. Returns self so it chains off new().
  function p:attach()
    if sched then sched.pump(function() self:step() end) end
    return self
  end

  return p
end
