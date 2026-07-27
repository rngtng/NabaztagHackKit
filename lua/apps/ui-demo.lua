-- Event-driven reactive resident for the simulator browser UI (#43). Registers
-- nab.on callbacks and RETURNS (no blocking loop), so the firmware drops to the
-- REPL prompt: the cooperative pump (#195) fires these callbacks while the
-- prompt idles, so the live view keeps reacting to injected input AND you can
-- still type Lua at the in-browser REPL (both share the one console).
--   task lua:simui:serve            (present tags / hold the button; type Lua)
-- Green tag -> all LEDs green + left ear spins; yellow tag -> yellow + right
-- ear; no tag -> dark, ears stopped; button held -> all LEDs white.
-- REPL note: each line is its own #LC chunk, so functions are one line and
-- state lives in globals (same as apps/rfid-led-ears.lua).
GREEN_UID = "d0021a3506198b86"
YELLOW_UID = "d0021a35038f3a2f"
function allled(r,g,b) nab.led('nose',r,g,b) nab.led('belly',r,g,b) nab.led('bottom',r,g,b) nab.led('left',r,g,b) nab.led('right',r,g,b) end
function tagmode(t) if t == GREEN_UID then allled(0,127,0) nab.ear_move(1,'forward') nab.ear_stop(2) elseif t == YELLOW_UID then allled(127,127,0) nab.ear_move(2,'forward') nab.ear_stop(1) else allled(0,0,0) nab.ear_stop(1) nab.ear_stop(2) end end
nab.on('rfid', function(u) tagmode(u) end)
nab.on('button', function(d) if d then allled(127,127,127) else tagmode(nab.rfid()) end end)
