-- #123 wheel demo: a continuous tone whose volume AND LED brightness follow
-- the back volume wheel live. The wheel is an analog pot on ADC ch.2
-- (hardware-verified: 255 at rest, 255 -> 0 across its travel), so:
--
--   rest (255)  -> quiet + dark
--   full  (0)   -> loud + bright orange
--
-- nab.play is blocking per buffer, so the "continuous" tone is the built-in
-- MP3 tone replayed in a loop; the wheel is re-read between chunks (a chunk
-- is short, so it tracks the hand well). Attenuation is wheel//2 (0..127 =
-- 0..-63 dB) - the raw 0..254 range is inaudible over most of its travel.
-- LEDs use nab.led8 (instant, gamma) rather than fades: brightness must jump
-- with the wheel, not ease toward it.
--
-- Exits on the head button, or after ~60 s as a capture-run safety net.
--
-- Hardware-only (sim models neither DREQ nor ADC); scripted REPL feed is
-- broken at 115200 (#276) - run it via the boot chunk like volume-demo.lua.

all5 = { 'nose', 'belly', 'left', 'right', 'bottom' }

function led_all(r, g, b) for i = 1, #all5 do nab.led8(all5[i], r, g, b) end end

function wheel_volume_demo()
  print('wheel demo (#123): turn the back wheel = volume + LED brightness; button stops')
  local t0, last = nab.time(), -1
  repeat
    local w = nab.wheel()          -- 255 rest .. 0 full
    nab.volume(w // 2)             -- 0 = loudest .. 127 = -63 dB
    local b = 255 - w              -- brightness tracks loudness
    led_all(b, b // 3, 0)
    if last < 0 or (w > last and w - last > 7) or (last > w and last - w > 7) then
      print(string.format('  wheel %3d -> vol -%2d dB, led %3d', w, w // 4, b))
      last = w
    end
    nab.play(nab.tone())
  until nab.button() or nab.time() - t0 > 60000
  led_all(0, 0, 0)
  print('wheel demo done')
end

wheel_volume_demo()
