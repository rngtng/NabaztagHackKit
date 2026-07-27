-- #123 acceptance demo: nab.volume attenuates nab.play (VS1003 SDI decode),
-- shown as 5 descending volume steps (0 -> -40 dB) with the LED brightness
-- synced to the loudness (bright orange -> faint ember), two passes.
--
-- Hardware-only: the sim models neither DREQ (nab.play) nor the codec, so
-- there is nothing to hear off-device. The scripted REPL feed is broken at
-- 115200 (#276), so to run it on the rig paste the body into the boot chunk
-- (lua/boot/boot.lua) behind a `nab.delay(3000)` and flash with CAPTURE=1 -
-- exactly how the #123 verification ran.
--
-- nab.fade runs off the 1 ms timer IRQ, so the LEDs keep animating while
-- nab.play busy-feeds SDI (hardware-verified).

all5 = { 'nose', 'belly', 'left', 'right', 'bottom' }
vols = { 0x00, 0x14, 0x28, 0x3C, 0x50 } -- VS1003 attenuation, 2 = -1 dB
brights = { 255, 128, 64, 28, 10 }      -- matched LED level

function fade_all(r, g, b, ms) for i = 1, #all5 do nab.fade(all5[i], r, g, b, ms) end end

function volume_demo()
  print('volume demo (#123): 5 descending steps, LEDs track loudness')
  fade_all(0, 0, 0, 200); nab.delay(300)
  for pass = 1, 2 do
    print('pass ' .. pass)
    for i = 1, #vols do
      local v, b = vols[i], brights[i]
      print(string.format('  step %d: vol 0x%02x (-%d dB), led %d', i, v, v // 2, b))
      nab.volume(v)
      fade_all(b, b // 3, 0, 150) -- orange, brightness tracks loudness
      nab.delay(180)
      nab.play(nab.tone())
      fade_all(0, 0, 0, 350)
      nab.delay(650)
    end
    nab.delay(800)
  end
  print('volume demo done')
end

volume_demo()
