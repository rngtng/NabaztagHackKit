-- Microphone hardware test (#116 mic half) - LED-guided, no interaction with
-- the console needed. Run on the rig:
--   task lua:firmware:repl:hw APP=lua SCRIPT=apps/mic-test.lua
-- LED legend (nose):  RED = RECORDING, speak/clap now!   BLUE = RECORDING,
-- stay silent!   WHITE = LISTEN to the speaker.   BLINKING red/blue =
-- cooperative recording, speak!   At the end:
-- all LEDs GREEN = every objective check passed (belly flags each check as it
-- runs: green pass / red fail). Your ears verify the WHITE phases.
-- NB: REPL constraint - one statement per line, globals only (no cross-line
-- locals), lines < 256 chars.
print('=== mic-test: nose RED=speak, WHITE=listen, end GREEN=pass ===')
pass = 0
fail = 0
function ok(name, cond) if cond then pass = pass + 1 nab.led('belly', 0, 127, 0) print('PASS', name) else fail = fail + 1 nab.led('belly', 127, 0, 0) print('FAIL', name) end end
--
print('--- T0 codec alive: LISTEN for beep + tone (nose white) ---')
nab.led('nose', 127, 127, 127)
nab.beep()
nab.play(nab.tone())
--
print('--- T1 blocking record: nose RED, speak for 2 s after the beep ---')
nab.beep(60, 100)
nab.led('nose', 127, 0, 0)
s = nab.record(2000)
nab.led('nose', 0, 0, 0)
print('T1 #s =', #s, '(expect 8252; 60 = codec never delivered)')
ok('T1 record fills', #s == 8252)
--
print('--- T2a nose BLUE = stay SILENT for 1 s ---')
nab.led('nose', 0, 0, 127)
q = nab.record(1000)
print('--- T2b nose RED = CLAP/SPEAK LOUD for 1 s after the beep ---')
nab.beep(60, 100)
nab.led('nose', 127, 0, 0)
l = nab.record(1000)
nab.led('nose', 0, 0, 0)
print('T2 block-1 header bytes 61..64 (predictor + step index):')
print('T2 quiet:', q:byte(61), q:byte(62), q:byte(63), q:byte(64))
print('T2 loud: ', l:byte(61), l:byte(62), l:byte(63), l:byte(64))
print('T2 note the byte in 0..88 that grew with loudness = VU step index')
ok('T2 data nonzero', l:find('[^\0]', 61) ~= nil)
ok('T2 quiet/loud differ', q:byte(61) ~= l:byte(61) or q:byte(62) ~= l:byte(62) or q:byte(63) ~= l:byte(63) or q:byte(64) ~= l:byte(64))
--
print('--- T3 playback: nose WHITE = LISTEN, this should be your loud clip ---')
nab.led('nose', 127, 127, 127)
nab.play(l)
--
print('--- T4 decode mode restored: LISTEN, tone again (slow/static = CLOCKF lost) ---')
nab.play(nab.tone())
nab.led('nose', 0, 0, 0)
--
print('--- T5 cooperative session: nose BLINKS red/blue ~1 s, speak! ---')
chunks = {}
n = 0
i = 0
nab.rec_start()
while n < 4096 and i < 10000 do i = i + 1 local c = nab.rec_read() if c then chunks[#chunks + 1] = c n = n + #c end nab.led('nose', (i % 256 < 128) and 127 or 0, 0, (i % 256 < 128) and 0 or 127) end
nab.rec_stop()
nab.led('nose', 0, 0, 0)
print('T5 chunks:', #chunks, 'bytes:', n, 'polls:', i)
ok('T5 collected 4 KB', n >= 4096)
print('--- T5 playback: nose WHITE = LISTEN, your cooperative clip ---')
nab.led('nose', 127, 127, 127)
if n > 0 then nab.play(nab.rec_wav(table.concat(chunks))) end
--
print('=== RESULT:', pass, 'passed,', fail, 'failed (of 4 objective checks) ===')
if fail == 0 then nab.led('nose', 0, 127, 0) nab.led('belly', 0, 127, 0) nab.led('left', 0, 127, 0) nab.led('right', 0, 127, 0) nab.led('bottom', 0, 127, 0) else nab.led('nose', 127, 0, 0) end
print('ALL GREEN = objective checks passed. Ears verified: T0 beep+tone, T3 your voice, T4 clean tone, T5 clip.')
