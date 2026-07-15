-- Resident boot chunk for the Lua firmware (#128).
--
-- The bytecode-only image has no on-device parser, so this cannot be compiled
-- at startup - the build compiles it off-device (tools/luac/embed.py) into
-- gen/boot_lc.h and lua.c loads that blob via luaL_loadbuffer. Keep it small:
-- it is baked into the 124 KB flash image.
--
-- It defines the M5 nab-binding demo helpers and sets an idle LED state, then
-- returns to the REPL. It does NOT auto-call run(): run() is a while-true RFID
-- loop that only returns on a head-button press, so calling it at boot would
-- strand the REPL behind a physical button on hardware (#207). Start the demo
-- yourself from the prompt: run().

GREEN_UID = "d0021a3506198b86"
YELLOW_UID = "d0021a35038f3a2f"
LEFT_MOTOR = 1
RIGHT_MOTOR = 2

function allled(r, g, b)
  nab.led('nose', r, g, b)
  nab.led('belly', r, g, b)
  nab.led('left', r, g, b)
  nab.led('right', r, g, b)
  nab.led('bottom', r, g, b)
end

function greenmode()
  allled(0, 127, 0)
  nab.ear_move(LEFT_MOTOR, 255, 'forward')
end

function yellowmode()
  allled(127, 127, 0)
  nab.ear_move(RIGHT_MOTOR, 255, 'forward')
end

function colormode()
  nab.led('nose', 127, 0, 0)
  nab.led('belly', 127, 127, 0)
  nab.led('left', 0, 127, 0)
  nab.led('right', 0, 0, 127)
  nab.led('bottom', 127, 0, 127)
end

function blackmode()
  allled(0, 0, 0)
  nab.led('bottom', 0, 0, 127)
  nab.ear_stop(LEFT_MOTOR)
  nab.ear_stop(RIGHT_MOTOR)
end

function react(t)
  if t == GREEN_UID then
    greenmode()
  elseif t == YELLOW_UID then
    yellowmode()
  else
    blackmode()
  end
end

function run()
  while true do
    react(nab.rfid())
    if nab.ear_pos(LEFT_MOTOR) == nab.ear_pos(RIGHT_MOTOR) then
      colormode()
    end
    if nab.button() then
      blackmode()
      return
    end
  end
end

blackmode() -- idle LED state; then the REPL. Type run() to start the demo.
