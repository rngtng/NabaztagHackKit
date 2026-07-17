-- Resident boot chunk for the Lua firmware (#128).
--
-- The bytecode-only image has no on-device parser, so this cannot be compiled
-- at startup - the build compiles it off-device (tools/luac/embed.py) into
-- gen/boot_lc.h and lua.c loads that blob via luaL_loadbuffer. Keep it small:
-- it is baked into the 124 KB flash image.
--
-- It defines the M5 nab-binding demo helpers plus a short LED showcase (#102),
-- sets an idle LED state, then returns to the REPL. It does NOT auto-run
-- anything long: run() is a while-true RFID loop that only returns on a
-- head-button press, and ledshow() is a ~6 s animation - auto-calling either at
-- boot would delay or strand the REPL (the boot chunk eats the instruction
-- budget before the prompt, #207). Both bodies are resident, so trigger them
-- with a short line at the prompt: run() or ledshow(). ledshow() doubles as a
-- timer/fade self-test - smooth breathing means the 1 ms timer IRQ + background
-- fade engine work; a jump means the timer isn't firing. The fuller show is
-- ../apps/led-demo.lua.

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
  nab.ear_move(LEFT_MOTOR, 'forward')
end

function yellowmode()
  allled(127, 127, 0)
  nab.ear_move(RIGHT_MOTOR, 'forward')
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

-- Event-driven variant of run() (#195): register callbacks and return to the
-- REPL. react() then fires from the cooperative pump (REPL idle / nab.wait)
-- when a tag lands or leaves the coupler; a button press stops watching and
-- goes dark. No busy loop, and the prompt stays usable while it watches.
function watch()
  nab.on('rfid', react)
  nab.on('button', function(pressed)
    if pressed then
      nab.on('rfid', nil)
      nab.on('button', nil)
      blackmode()
    end
  end)
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

-- Short boot LED showcase (#102): breathe all five LEDs blue then magenta, then
-- run a ball round the belly ring. nab.fade runs in the background off the 1 ms
-- timer IRQ; nab.delay paces the frames off the same clock. Bounded (~6 s) so it
-- never strands the REPL (contrast run(), #207).
BELLY_RING = { 'belly', 'right', 'bottom', 'left' }
ALL_LEDS = { 'nose', 'belly', 'left', 'right', 'bottom' }

function fade5(r, g, b, ms)
  for i = 1, #ALL_LEDS do nab.fade(ALL_LEDS[i], r, g, b, ms) end
end

function ledshow()
  print('LED showcase (#102): breathe + ring')
  for i = 1, #ALL_LEDS do nab.led8(ALL_LEDS[i], 0, 0, 0) end
  fade5(0, 120, 230, 700); nab.delay(900)
  fade5(230, 0, 120, 700); nab.delay(900)
  fade5(0, 0, 0, 600); nab.delay(700)
  for i = 1, #BELLY_RING do
    nab.fade(BELLY_RING[i], 120, 170, 255, 80); nab.delay(150)
    nab.fade(BELLY_RING[i], 0, 0, 0, 430)
  end
  nab.delay(500); fade5(0, 0, 0, 300)
  print('LED showcase done')
end

blackmode() -- idle LED state; then the REPL. Type run() or ledshow() to start.
