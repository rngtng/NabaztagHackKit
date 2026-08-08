-- audio.midi: note names, SMF variable-length quantities, and a complete
-- one-note file compared byte for byte against the Standard MIDI File spec
-- (header chunk + one track chunk, hand-derived - not "what the module said").

local midi = audio.midi

-- note names -----------------------------------------------------------------

eq(midi.num("C4"), 60, "C4 is middle C = 60")
eq(midi.num("A4"), 69, "A4 = 69 (the 440 Hz one)")
eq(midi.num("C-1"), 0, "C-1 = 0, the bottom of the range")
eq(midi.num("G9"), 127, "G9 = 127, the top")
eq(midi.num("F#5"), 78, "sharps raise a semitone")
eq(midi.num("Bb3"), 58, "flats lower a semitone")
eq(midi.num("bb3"), 58, "case-insensitive")
eq(midi.num(64), 64, "a number passes through")
eq(midi.num(nil), nil, "nil is a rest")
eq(midi.num("r"), nil, "'r' is a rest")
eq(midi.num("H4"), nil, "a nonsense name is not a note")

-- variable-length quantities (SMF spec table) --------------------------------

eq(X(midi.varlen(0)), "00", "varlen 0")
eq(X(midi.varlen(0x40)), "40", "varlen 0x40")
eq(X(midi.varlen(0x7F)), "7f", "varlen 0x7F - still one byte")
eq(X(midi.varlen(0x80)), "8100", "varlen 0x80 - two bytes")
eq(X(midi.varlen(0x2000)), "c000", "varlen 0x2000")
eq(X(midi.varlen(0x3FFF)), "ff7f", "varlen 0x3FFF")
eq(X(midi.varlen(0x100000)), "c08000", "varlen 0x100000")

-- one note, whole file -------------------------------------------------------

-- MThd len 6, format 0, 1 track, 480 ticks/quarter
-- MTrk len 0x17: tempo 500000 (07a120), program 9, note-on C4 vel 100,
--                480 ticks (varlen 8360) later note-off, end of track
local ONE = "4d546864 00000006 0000 0001 01e0"
         .. "4d54726b 00000017"
         .. "00 ff5103 07a120"
         .. "00 c009"
         .. "00 903c64"
         .. "8360 803c40"
         .. "00 ff2f00"

local f = midi.note("C4", 500)
eq(X(f), (ONE:gsub("%s", "")), "a one-note file is byte-exact SMF")
eq(f:sub(1, 4), "MThd", "starts with the header chunk")
eq(f:sub(15, 18), "MTrk", "then the track chunk")
eq(#f, 14 + 8 + 0x17, "length = header + track header + track")

-- 500 ms is exactly one quarter note at the default tempo
eq(X(midi.varlen(480)), "8360", "480 ticks encodes as 83 60")

-- the resident tone: the same file, built on the other side of the seam ------

-- #331 swapped nab.tone()'s 2,160 B MP3 for a 45-byte SMF that
-- firmware/tools/tonegen.py builds from these same constants, so the resident
-- asset and a Lua-built jingle cannot drift in pitch or tempo conventions.
-- Nothing is shared across the seam except the constants, which is exactly why
-- both sides need pinning: firmware/test/host/tone_test.c holds the C half
-- against the SMF spec, and this is the Lua half held against the same bytes.
-- If this fails, one side moved - regenerate with `task lua:firmware:gen:tone`
-- and check tone_test still passes before believing the new bytes.
local TONE = "4d546864 00000006 0000 0001 01e0"
          .. "4d54726b 00000017"
          .. "00 ff5103 07a120"    -- tempo 500000 = midi.TEMPO
          .. "00 c009"             -- program 9 = midi.PROGRAM (glockenspiel)
          .. "00 905164"           -- note on A5 (81 = 0x51), velocity 100
          .. "8170 805140"         -- 240 ticks = 250 ms, then note off
          .. "00 ff2f00"

eq(X(midi.note("A5", 250)), (TONE:gsub("%s", "")),
   "audio.midi.note('A5', 250) IS inc/tone_midi.h, byte for byte")
eq(#midi.note("A5", 250), 45, "...all 45 bytes of it")
eq(midi.num("A5"), 81, "A5 = 81, the 880 Hz the MP3 predecessor synthesised")

-- a rest delays the next note instead of emitting one ------------------------

local r = midi.tune{{nil, 250}, {"C4", 250}}
eq(select(2, r:gsub("\x90", "")), 1, "a rest emits no note-on")
ok(r:find("\x81\x70\x90\x3c", 1, true), "the rest's 240 ticks delay the note-on")

local tail = midi.tune{{"C4", 250}, {"r", 500}}
ok(tail:find("\x83\x60\xff\x2f\x00", 1, true),
   "a trailing rest lands in the end-of-track delta")

-- a tune is the notes in order, each one note-on/note-off --------------------

local t = midi.tune{{"C5", 120}, {"E5", 120}, {"G5", 240}}
eq(select(2, t:gsub("\x90", "")), 3, "three note-ons")
eq(select(2, t:gsub("\x80", "")), 3, "three note-offs")
local notes = {}
for n in t:gmatch("\x90(.)") do notes[#notes + 1] = n:byte() end
eq(table.concat(notes, ","), "72,76,79", "C5 E5 G5, in that order")
ok(#t < 100, "a three-note jingle is under 100 bytes")

-- options: tempo, program and velocity are in the file -----------------------

local o = midi.tune({{"C4", 100}}, {tempo = 250000, program = 0x18,
                                    velocity = 0x33})
ok(o:find("\xff\x51\x03\x03\xd0\x90", 1, true), "tempo 250000 written")
ok(o:find("\xc0\x18", 1, true), "program change written")
ok(o:find("\x90\x3c\x33", 1, true), "velocity written")
-- twice as fast a quarter note = twice the ticks for the same 100 ms
-- (96 ticks at the default tempo, 192 = varlen 81 40 here)
ok(o:find("\x81\x40\x80\x3c", 1, true), "duration scales with the tempo")

-- an empty tune is still a valid (silent) file
local e = midi.tune{}
eq(X(e:sub(1, 14)), "4d5468640000000600000001" .. "01e0", "header intact")
eq(#e, 14 + 8 + 14, "just tempo, program and end-of-track")
