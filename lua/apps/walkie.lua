-- Walkie-talkie demo (#275 acceptance): HOLD the head button to record
-- (nose RED), release to play it back (nose WHITE, then GREEN; dim BLUE =
-- ready). Shows the cooperative record API (rec_start/rec_read/rec_stop +
-- rec_wav) against nab.button() polling. Record gain is AGC (default), so
-- playback volume settles over the first seconds.
-- Run on the rig: bake it into the boot chunk (lua/boot/boot.lua - back the
-- original up, restore after) and `task lua:firmware:flash CAPTURE=1
-- RUN_TIMEOUT=240`. Not a SCRIPT= run - the REPL frames a .lua one line per
-- frame, which this multi-line loop cannot survive, and the loop never returns
-- to the prompt anyway. In the simulator it needs the same .lc treatment plus
-- an injected button timeline: task lua:apps:compile APP=apps/walkie.lua &&
-- task lua:apps:simulate APP=apps/walkie.lc INJECT=<timeline.jsonl>
-- (the mic is unmodeled there, so it exercises the loop, not the audio).
print('=== walkie: HOLD button to record (nose RED), release to play back ===')
while true do
  nab.led('nose', 0, 0, 24)
  while not nab.button() do nab.delay(20) end
  nab.led('nose', 127, 0, 0)
  nab.rec_start()
  local chunks = {}
  while nab.button() do
    local c = nab.rec_read()
    if c then chunks[#chunks + 1] = c end
  end
  nab.rec_stop()
  local data = table.concat(chunks)
  print('recorded bytes', #data)
  nab.led('nose', 127, 127, 127)
  if #data > 0 then nab.play(nab.rec_wav(data)) end
  nab.led('nose', 0, 127, 0)
  nab.delay(300)
end
