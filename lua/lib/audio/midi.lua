-- audio.midi - jingles as Standard MIDI Files (#265), the lua-track answer to
-- mtl/lib/audio/midi.mtl.
--
-- The VS1003B decodes MIDI natively, so a jingle is just another byte string
-- for nab.play / audio.player - no synthesis, no samples, ~60 bytes per note.
-- mtl shipped a table of pre-built SMF blobs (one per note, hex-escaped);
-- building the file instead costs less to carry and lets a script write its
-- own tune:
--
--   nab.play(audio.midi.tune{{"C5", 120}, {"E5", 120}, {"G5", 240}})
--   p:play(audio.midi.note("A4", 400))
--
-- Format 0 (one track), DIVISION ticks per quarter note, one tempo meta event
-- and one program change up front - the smallest file a decoder accepts.
-- Durations are given in ms and converted with integer arithmetic only (#213:
-- no float may cross a vararg on the device).

audio = audio or {}
local midi = {}
audio.midi = midi

midi.DIVISION = 480      -- ticks per quarter note
midi.TEMPO = 500000      -- microseconds per quarter note (= 120 bpm)
midi.PROGRAM = 9         -- GM 10, glockenspiel - the V1 jingle voice
midi.VELOCITY = 100      -- default note-on velocity

-- ms -> ticks, integer: ticks = ms * DIVISION / (tempo us / 1000). Uses the
-- tempo actually written into the file, so o.tempo keeps note *durations*
-- (which are given in ms) honest instead of silently rescaling them.
local function ticks(ms, tempo)
  return ms * midi.DIVISION // (tempo // 1000)
end

local SEMI = {C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11}

-- "C4" / "F#5" / "Bb3" -> MIDI note number (C4 = 60, i.e. middle C).
-- A number passes through; nil (or "r") is a rest and stays nil.
function midi.num(n)
  if type(n) == "number" then return n end
  if n == nil or n == "r" then return nil end
  local letter, acc, oct = n:upper():match("^([A-G])([#B]?)(%-?%d+)$")
  if not letter then return nil end
  local v = SEMI[letter] + (tonumber(oct) + 1) * 12
  if acc == "#" then v = v + 1 elseif acc == "B" then v = v - 1 end
  return v
end

-- SMF variable-length quantity (7 bits per byte, high bit = "more follow")
function midi.varlen(n)
  local out = string.char(n & 0x7F)
  n = n >> 7
  while n > 0 do
    out = string.char((n & 0x7F) | 0x80) .. out
    n = n >> 7
  end
  return out
end

-- notes = { {note, ms [, velocity]}, ... } where note is a name ("C4"), a MIDI
-- number, or nil/"r" for a rest. Monophonic by design (a rabbit jingle).
-- o = {program=, tempo=, velocity=} -> a complete .mid file as a byte string.
function midi.tune(notes, o)
  o = o or {}
  local tempo = o.tempo or midi.TEMPO
  local ev = {midi.varlen(0) .. "\255\81\3"
              .. string.pack(">I3", tempo),                   -- FF 51 03 tempo
              midi.varlen(0) .. string.char(0xC0, o.program or midi.PROGRAM)}
  local rest = 0   -- ticks of silence owed to the next note-on

  for i = 1, #notes do
    local n, ms, vel = notes[i][1], notes[i][2] or 0, notes[i][3]
    local num = midi.num(n)
    if num == nil then
      rest = rest + ticks(ms, tempo)
    else
      ev[#ev + 1] = midi.varlen(rest)
                    .. string.char(0x90, num & 0x7F,
                                   vel or o.velocity or midi.VELOCITY)
      ev[#ev + 1] = midi.varlen(ticks(ms, tempo))
                    .. string.char(0x80, num & 0x7F, 0x40)
      rest = 0
    end
  end
  ev[#ev + 1] = midi.varlen(rest) .. "\255\47\0"   -- FF 2F 00 end of track

  local track = table.concat(ev)
  return "MThd" .. string.pack(">I4I2I2I2", 6, 0, 1, midi.DIVISION)
         .. "MTrk" .. string.pack(">I4", #track) .. track
end

-- one note (or a rest), same arguments as a tune entry
function midi.note(n, ms, o)
  return midi.tune({{n, ms}}, o)
end
