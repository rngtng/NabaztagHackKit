-- RFID-reactive LEDs + ear spin, exit on button (M5/M9/M10 bindings).
--   task repl:firmwareV2:hw APP=lua SCRIPT=src/firmwareV2/examples/rfid-led-ears.lua
-- Green tag held -> all 5 LEDs green + left ear spins. Yellow tag held -> all
-- 5 LEDs yellow + right ear spins. No tag -> LEDs black, both ears stopped.
-- Button held -> stop ears, exit the loop (drops back to the REPL prompt).
-- Each line below is fed to the REPL as its own chunk (see repl-demo.lua), so
-- every function body must stay on one physical line, state lives in globals,
-- AND every line must stay under lua.c's REPL_LINE (256 bytes) - a line that
-- long gets silently truncated by sh_gets, producing a syntax error that
-- leaves the function undefined with no obvious runtime symptom beyond "the
-- feature never runs" (confirmed the hard way: verify with the no-hardware
-- simulator - task repl:firmwareV2 SCRIPT=... - before burning a JTAG round
-- trip, since it prints the exact same REPL compile errors for free).
-- NB: nab.led's 5 names (nose/belly/bottom/left/right) are LED positions, not
-- the ear sides below - naming collision, not a bug.
GREEN_UID = "d0021a3506198b86"   -- confirmed on hardware (task repl:firmwareV2:hw)
YELLOW_UID = "d0021a35038f3a2f" -- confirmed on hardware (task repl:firmwareV2:hw)
LEFT_MOTOR = 1  -- TODO: confirm against hardware which motor number is physically the left ear (swap with RIGHT_MOTOR if backwards)
RIGHT_MOTOR = 2 -- TODO: the other motor number
function allled(r,g,b) nab.led('nose',r,g,b) nab.led('belly',r,g,b) nab.led('bottom',r,g,b) nab.led('left',r,g,b) nab.led('right',r,g,b) end
function greenmode() allled(0,127,0) nab.ear_move(LEFT_MOTOR,255,'forward') nab.ear_stop(RIGHT_MOTOR) end
function yellowmode() allled(127,127,0) nab.ear_move(RIGHT_MOTOR,255,'forward') nab.ear_stop(LEFT_MOTOR) end
function blackmode() allled(0,0,0) nab.ear_stop(LEFT_MOTOR) nab.ear_stop(RIGHT_MOTOR) end
function react(t) if t == GREEN_UID then greenmode() elseif t == YELLOW_UID then yellowmode() else blackmode() end end
function run() while true do react(nab.rfid()) if nab.button() then blackmode() return end end end
run()
