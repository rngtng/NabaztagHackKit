-- audio.record - cooperative microphone recording (#333), the Lua replacement
-- for the C `nab.record(ms)` binding.
--
-- `nab.record` blocked for up to 30 s while the codec filled its FIFO: no ear
-- stepped, no player was fed, no sched task resumed, nothing queued was
-- delivered. It was one of the seam's freezes, and it was 220 B of flash for
-- arithmetic and a poll loop - everything it did already existed on the seam in
-- cooperative form (nab.rec_start / nab.rec_read / nab.rec_stop / nab.rec_wav),
-- so this module costs no flash and gives the reactor its turn between polls.
--
-- Pull-style like player/ears: the HAL is injected, the caller owns the clock
-- and pumps :step(). :wait() is the blocking convenience, and it pumps.
--
--   audio.record(2000)                       -- REPL one-liner -> a WAV string
--
--   local r = audio.recorder.new(audio.recdrv())    -- ...or drive it yourself
--   r:start(2000)
--   while r:busy() do r:step(); leds_and_whatever_else() end
--   nab.play(r:wav())
--
-- **Poll it often.** The codec's ~2 KB FIFO holds about half a second at
-- 8 kHz; overflow drops audio (it never crashes), so a :step() gap longer than
-- that loses sound. That is the whole reason the session API exists, and the
-- reason :wait() uses sleep(0) - a pump-once with no delay - rather than
-- sleeping between polls the way ears:wait() can afford to.
--
-- NB on the seam: nab.wait() inside the reactor does NOT pump. dispatch_events
-- carries a re-entrancy guard, so from inside a sched pump or task it is just
-- dead air (#329). That is why the sleep here comes off the injected drv and
-- why :attach() exists: a recorder inside the reactor is driven BY the reactor,
-- not by a nested wait.

audio = audio or {}
local rec = {}
audio.recorder = rec

-- IMA ADPCM as the VS1003 delivers it at 8 kHz: whole 256-byte blocks carrying
-- 505 samples each, i.e. 8000 / 505 * 256 = 4055 bytes per second. That is the
-- same byte rate utils/wav.c writes into the RIFF header, and the reason the
-- old C constant was 4055 rather than anything derivable from 8000 alone.
rec.RATE = 4055
rec.BLOCK = 256

-- Give-up bounds for a codec that stops delivering: the C path passed a poll
-- bound (REC_WAIT, "~20x one block time") to a blocking read and returned
-- short when it expired. One 256-byte block is ~63 ms, so STALL is ~16 blocks
-- of silence from the FIFO. STALL_STEPS is the same rule counted in steps, for
-- a clock that does not move - the simulator has no DREQ model, so rec_read
-- there never yields a byte and a purely time-based bound could spin (same
-- belt-and-braces as audio.player).
rec.STALL = 1000
rec.STALL_STEPS = 50000

-- the real hardware
function audio.recdrv()
  return {start = nab.rec_start, read = nab.rec_read, stop = nab.rec_stop,
          wav = nab.rec_wav, time = nab.time, sleep = nab.wait}
end

-- nab.time() is a wrapping 32-bit tick (#259): differences modulo its width.
local function elapsed(now, t0)
  return (now - t0) & 0xFFFFFFFF
end

-- ms -> bytes of ADPCM, rounded UP to a whole 256-byte block, because the
-- codec only ever delivers whole blocks and the WAV header counts samples as
-- blocks * 505. Byte-identical to what nab.record computed.
function rec.bytes(ms)
  local n = (ms * rec.RATE // 1000 + rec.BLOCK - 1) & ~(rec.BLOCK - 1)
  if n < rec.BLOCK then n = rec.BLOCK end
  return n
end

-- drv = {start=fn([gain]), read=fn()->chunk|nil, stop=fn(), wav=fn(s)->s,
--        time=fn()->ms, sleep=fn(ms)}  -- sleep only used by :wait()
function rec.new(drv)
  local self = {drv = drv, chunks = {}, got = 0, want = 0, state = "idle",
                open = false, short = false}

  -- close the session, but only one we actually opened: :stop() on an idle
  -- recorder must not put the codec back into decode mode behind someone's
  -- back (the same rule audio.player has for the decoder stream).
  local function close(s)
    if s.open then
      s.open = false
      drv.stop()
    end
    s.state = "idle"
  end

  -- Record ~ms of audio (1..30000, as nab.record allowed), gain as on the
  -- seam: 1024 = 1x, 512 = 0.5x, nil/0 = the codec's automatic gain control.
  -- Returns self, so `r:start(2000):wait()` reads.
  function self:start(ms, gain)
    close(self)
    self.chunks, self.got, self.short = {}, 0, false
    self.want = rec.bytes(ms)
    self.tprog, self.idle_steps = drv.time(), 0
    self.state, self.open = "recording", true
    drv.start(gain)
    return self
  end

  -- One turn: drain whatever whole blocks the FIFO has. Returns the state it
  -- left the recorder in - "recording" | "idle" - and never blocks.
  function self:step()
    if self.state ~= "recording" then return self.state end
    local c = drv.read()

    if c and #c > 0 then
      self.chunks[#self.chunks + 1] = c
      self.got = self.got + #c
      self.tprog, self.idle_steps = drv.time(), 0
      if self.got >= self.want then close(self) end
    else
      -- Nothing buffered yet is the normal case (a block takes ~63 ms to
      -- encode), so this is a bound, not an error - until it runs out, and
      -- then we keep what we have and say so via :short(). nab.record ended
      -- the same way: a recording can be shorter than asked, and the WAV
      -- header says how much.
      self.idle_steps = self.idle_steps + 1
      if elapsed(drv.time(), self.tprog) > rec.STALL
         or self.idle_steps > rec.STALL_STEPS then
        self.short = true
        close(self)
      end
    end
    return self.state
  end

  function self:busy() return self.state == "recording" end

  -- true if the codec stopped delivering before we had the whole recording
  function self:stalled() return self.short end

  -- Cut the recording now and keep what has been collected.
  function self:stop()
    close(self)
    return self
  end

  -- The raw ADPCM: whole 256-byte blocks, exactly the bytes nab.rec_wav wraps.
  -- Trimmed to what was asked for, because nab.rec_read hands back up to a
  -- whole FIFO drain and the last one can overshoot; the C loop could not
  -- overshoot because it passed the remaining count down to the read.
  function self:data()
    local d = table.concat(self.chunks)
    if #d > self.want then d = d:sub(1, self.want) end
    return d
  end

  -- ...and the same thing as a complete WAV file, which is what nab.record
  -- returned. The RIFF header stays on the seam (#327): nab.record's C path
  -- needed it either way, and it is byte-identical to the mtl stack's.
  function self:wav()
    return drv.wav(self:data())
  end

  -- Blocking convenience for the REPL: pump until the recording is done (or ms
  -- elapse). sleep(0) is a pump-once - everything else in the reactor still
  -- runs, which is the whole point of replacing nab.record. Apps call :step().
  function self:wait(ms)
    local t0 = drv.time()
    while self:busy() do
      self:step()
      if ms and elapsed(drv.time(), t0) > ms then return false end
      if drv.sleep then drv.sleep(0) end
    end
    return true
  end

  -- Hand :step() to the cooperative reactor (#283), so the FIFO keeps being
  -- drained from every nab.wait/nab.delay and from the REPL's idle loop - not
  -- only from a loop the app remembers to write. Guarded because `sched` is a
  -- device global and the host tests have no reactor in sight.
  function self:attach()
    if sched and not self.pumped then
      self.pumped = sched.pump(function() self:step() end)
    end
    return self
  end

  -- Counterpart to :attach(); see hw.ears for why it exists (#297).
  function self:detach()
    if self.pumped then
      sched.unpump(self.pumped)
      self.pumped = nil
    end
    return self
  end

  return self
end

-- audio.record(ms [, gain [, drv]]) -> a complete WAV string. The drop-in for
-- the removed nab.record: same arguments, same bytes out, except that the
-- reactor keeps running while it records. drv is for the host tests; on the
-- device leave it out.
function audio.record(ms, gain, drv)
  local r = rec.new(drv or audio.recdrv())
  r:start(ms, gain):wait()
  return r:wav()
end
