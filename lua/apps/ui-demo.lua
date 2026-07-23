-- Continuous reactive loop for the simulator browser UI (#43). Unlike run() in
-- rfid-led-ears.lua (which exits on a button press), this never returns, so the
-- live view stays reacting to injected input for as long as the sim runs:
--   task lua:simui:serve APP=apps/ui-demo.lua   (then present tags / hold the button)
-- Green tag -> all LEDs green + left ear spins; yellow tag -> yellow + right
-- ear; no tag -> dark, ears stopped; button held -> all LEDs white.
-- REPL note: each line is its own #LC chunk, so functions are one line and
-- state lives in globals (same as apps/rfid-led-ears.lua).
GREEN_UID = "d0021a3506198b86"
YELLOW_UID = "d0021a35038f3a2f"
function allled(r,g,b) nab.led('nose',r,g,b) nab.led('belly',r,g,b) nab.led('bottom',r,g,b) nab.led('left',r,g,b) nab.led('right',r,g,b) end
function tagmode(t) if t == GREEN_UID then allled(0,127,0) nab.ear_move(1,'forward') nab.ear_stop(2) elseif t == YELLOW_UID then allled(127,127,0) nab.ear_move(2,'forward') nab.ear_stop(1) else allled(0,0,0) nab.ear_stop(1) nab.ear_stop(2) end end
function loop() while true do if nab.button() then allled(127,127,127) else tagmode(nab.rfid()) end nab.delay(50) end end
loop()
