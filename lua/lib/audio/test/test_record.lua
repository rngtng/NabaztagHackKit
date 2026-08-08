-- audio.record: the Lua replacement for the C nab.record binding (#333).
--
-- Two things are actually at stake here, and both are asserted rather than
-- assumed:
--
--   * the arithmetic is byte-identical to the C path it replaces - the 4055
--     B/s ADPCM rate rounded up to whole 256-byte blocks. `apps/mic-test.lua`
--     asserted `#nab.record(2000) == 8252` against real hardware, so that
--     number is a measured fact about the codec, not a preference, and it is
--     pinned below;
--   * nothing blocks. The whole point of the move was that nab.record froze
--     the reactor for up to 30 s, so the tests check that a single :step()
--     returns with the recording unfinished and that :wait() pumps.
--
-- The fake codec hands back distinguishable blocks (block k is 256 copies of
-- byte k), so a test can say WHICH audio arrived and in what order - a length
-- check alone would pass on 8192 zero bytes, which is exactly what a dead
-- codec produces.

local rec = audio.recorder

-- A fake VS1003 record session. `blocks` is how many 256-byte blocks it will
-- ever deliver, `per` how many it hands back per successful read (a FIFO
-- drain), and `every` makes only every Nth read succeed - the FIFO taking
-- ~63 ms to fill another block is the normal case, not an error.
local function FAKEMIC(o)
  o = o or {}
  local m = {started = 0, stopped = 0, gain = false, t = 0, reads = 0,
             sleeps = 0, sent = 0, open = false,
             blocks = o.blocks or 0, per = o.per or 1, every = o.every or 1}

  m.drv = {
    start = function(g) m.started = m.started + 1; m.gain = g; m.open = true end,
    stop  = function() m.stopped = m.stopped + 1; m.open = false end,
    time  = function() return m.t end,
    sleep = function() m.sleeps = m.sleeps + 1; m.t = m.t + 1 end,
    wav   = function(s) return "RIFF...." .. s end,   -- stands in for nab.rec_wav
    read  = function()
      m.reads = m.reads + 1
      if not m.open or m.reads % m.every ~= 0 then return nil end
      local n = m.per
      if n > m.blocks then n = m.blocks end
      if n <= 0 then return nil end
      m.blocks = m.blocks - n
      local out = {}
      for _ = 1, n do
        m.sent = m.sent + 1
        out[#out + 1] = string.rep(string.char(m.sent % 256), 256)
      end
      return table.concat(out)
    end,
  }
  return m
end

-- what block k's 256 bytes look like
local function BLOCK(k) return string.rep(string.char(k % 256), 256) end

-- the length arithmetic, against the C path -----------------------------------

-- 8 kHz IMA ADPCM at 4055 B/s, rounded up to whole 256-byte blocks. The 2000 ms
-- case is the one nab.record was measured on: 8192 data bytes + the 60-byte
-- RIFF header = the 8252 apps/mic-test.lua asserts on hardware.
eq(rec.bytes(2000), 8192, "2 s is 8192 bytes (8252 with the 60-byte header)")
eq(rec.bytes(1000), 4096, "1 s rounds up to 4096")
eq(rec.bytes(30000), 121856, "the old 30 s ceiling is 121856 bytes")
eq(rec.bytes(1), 256, "1 ms still costs a whole block - the codec has no smaller unit")
eq(rec.bytes(63), 256, "one block's worth of time is one block")
eq(rec.bytes(64), 512, "...and a millisecond more is two")
eq(rec.bytes(2000) % 256, 0, "every length is a whole number of blocks")

-- a complete recording ---------------------------------------------------------

local m = FAKEMIC{blocks = 32}          -- exactly 2 s worth
local r = rec.new(m.drv)
r:start(2000, 512)

eq(m.started, 1, "start opens the session exactly once")
eq(m.gain, 512, "the gain is passed through to the codec")
ok(r:busy(), "and the recorder is recording")

ok(r:wait(), "wait() returns true when the recording completes")
eq(r:busy(), false, "...and the recorder is idle again")
eq(m.stopped, 1, "the session is closed exactly once")
eq(r:stalled(), false, "a full recording did not stall")

local data = r:data()
eq(#data, 8192, "2 s of audio is 8192 bytes")
eq(data:sub(1, 256), BLOCK(1), "the first block is the first one recorded")
eq(data:sub(257, 512), BLOCK(2), "...then the second")
eq(data:sub(-256), BLOCK(32), "...and the last block is the last one recorded")
ok(not data:find("^\0+$"), "the audio is not all-zero (a dead codec's signature)")
eq(r:wav(), "RIFF...." .. data, "wav() wraps exactly that data, header first")

-- nothing blocks ---------------------------------------------------------------

-- The regression the whole issue is about: nab.record ran the loop to
-- completion inside one call. One :step() must come back with the recording
-- still unfinished, so a caller can do its own work between polls.
local m2 = FAKEMIC{blocks = 32}
local r2 = rec.new(m2.drv)
r2:start(2000)
eq(r2:step(), "recording", "one step does not run the recording to the end")
ok(#r2:data() < 8192, "...it has only what the FIFO held")

-- and driven from a caller's loop, that caller's work runs between polls
local turns = 0
while r2:busy() do r2:step(); turns = turns + 1 end
ok(turns > 1, "the caller's loop got " .. turns .. " turns during the recording")
eq(#r2:data(), 8192, "and the recording is complete")
eq(m2.sleeps, 0, "a caller pumping :step() itself never sleeps")

-- :wait() pumps rather than sleeping through the recording
local m3 = FAKEMIC{blocks = 8, every = 3}
local r3 = rec.new(m3.drv)
r3:start(500):wait()
ok(m3.sleeps > 1, "wait() gave the reactor " .. m3.sleeps .. " pump-onces")
eq(#r3:data(), 2048, "and an every-3rd-read FIFO still fills the recording")

-- a slow FIFO is not an error --------------------------------------------------

local m4 = FAKEMIC{blocks = 16, every = 5, per = 4}
local r4 = rec.new(m4.drv)
r4:start(1000):wait()
eq(r4:stalled(), false, "four blocks every fifth poll is normal, not a stall")
eq(#r4:data(), 4096, "1 s recorded through a bursty FIFO")
eq(r4:data():sub(1, 256), BLOCK(1), "the burst's blocks stay in order")
eq(r4:data():sub(1025, 1280), BLOCK(5), "...across burst boundaries too")

-- an overshooting drain is trimmed ---------------------------------------------

-- nab.rec_read hands back a whole FIFO drain, so the last read can carry more
-- than was asked for; the C loop could not overshoot, because it passed the
-- remaining count down into the read.
local m5 = FAKEMIC{blocks = 64, per = 10}
local r5 = rec.new(m5.drv)
r5:start(1000):wait()                    -- wants 4096 = 16 blocks, gets 10 at a time
eq(#r5:data(), 4096, "an overshooting drain is trimmed to what was asked for")
eq(#r5:data() % 256, 0, "...on a block boundary, so rec_wav still accepts it")
eq(r5:data():sub(-256), BLOCK(16), "the trim keeps the FIRST 16 blocks")

-- the codec stops delivering ---------------------------------------------------

-- The simulator has no DREQ model and a wedged chip behaves the same way:
-- rec_read never yields a byte. nab.record returned short (header-only, in the
-- sim) rather than hanging, and so must this.
local m6 = FAKEMIC{blocks = 0}
local r6 = rec.new(m6.drv)
r6:start(2000)
ok(r6:wait(), "a codec that never delivers still terminates")
ok(r6:stalled(), "...and says it stalled")
eq(r6:data(), "", "with no audio")
eq(m6.stopped, 1, "the session is closed even so")
eq(r6:wav(), "RIFF....", "which is the header-only WAV nab.record produced")

-- a codec that delivers, then stops, keeps what it gave
local m7 = FAKEMIC{blocks = 3}
local r7 = rec.new(m7.drv)
r7:start(2000):wait()
ok(r7:stalled(), "a recording cut short by the codec reports it")
eq(#r7:data(), 768, "and keeps the three blocks that did arrive")
eq(r7:data():sub(513), BLOCK(3), "including the last one")

-- stopping early ---------------------------------------------------------------

local m8 = FAKEMIC{blocks = 32}
local r8 = rec.new(m8.drv)
r8:start(2000)
r8:step()
r8:step()
r8:stop()
eq(r8:busy(), false, "stop() ends the recording")
eq(m8.stopped, 1, "...and closes the session")
eq(#r8:data(), 512, "keeping the two blocks collected so far")
eq(r8:step(), "idle", "a step after stop reads nothing")
eq(m8.reads, 2, "...it really does not touch the codec")

-- stop() on a recorder that never started must not put the codec into decode
-- mode behind another session's back (audio.player has the same rule)
local m9 = FAKEMIC{blocks = 4}
local r9 = rec.new(m9.drv)
r9:stop()
eq(m9.stopped, 0, "stop() on an idle recorder does not touch the codec")
eq(m9.started, 0, "...nor open one")

-- restarting reuses the recorder and drops the old audio
local m10 = FAKEMIC{blocks = 64}
local r10 = rec.new(m10.drv)
r10:start(1000):wait()
eq(#r10:data(), 4096, "first recording")
r10:start(500):wait()
eq(#r10:data(), 2048, "second recording is its own length")
eq(r10:data():sub(1, 256), BLOCK(17), "...and its own audio, not the first's")
eq(m10.started, 2, "two sessions opened")
eq(m10.stopped, 2, "two sessions closed")

-- the convenience wrapper --------------------------------------------------------

-- audio.record(ms) is the drop-in for the removed nab.record: same arguments,
-- a complete WAV out.
local m11 = FAKEMIC{blocks = 16}
local w = audio.record(1000, 1024, m11.drv)
local want11 = {}
for k = 1, 16 do want11[#want11 + 1] = BLOCK(k) end
eq(w, "RIFF...." .. table.concat(want11),
   "audio.record returns the header followed by every block, in order")
eq(#w, 8 + 4096, "1 s through the wrapper is a 4096-byte recording")
eq(m11.gain, 1024, "the wrapper passes the gain through")
eq(m11.started, 1, "one session")
eq(m11.stopped, 1, "...closed")

-- :wait(ms) gives up rather than spinning forever ---------------------------------

-- STALL would end this one anyway; the point is that the caller's own bound is
-- honoured first and reports failure, the way player:wait(ms) does.
local m12 = FAKEMIC{blocks = 0}
local r12 = rec.new(m12.drv)
r12:start(30000)
eq(r12:wait(3), false, "wait(ms) returns false when its own deadline passes")
ok(r12:busy(), "...leaving the recording open for the caller to decide")
r12:stop()
