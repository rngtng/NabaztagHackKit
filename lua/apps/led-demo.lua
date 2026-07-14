-- LED showcase for firmwareV2 (issues #102 / #45): gamma fades with no low-end
-- dead zone + the background fade engine, shown off with a handful of effects.
--
--   Simulator: task lua:firmware:simulate:leddemo   (live 5-LED view; stops itself at the end)
--   Hardware:  the boot chunk (src/app/boot.lua) has a short resident showcase -
--              type `ledshow()` at the REPL (breathe + ring), a 10-char feed. It
--              is NOT auto-run at boot (that would delay the prompt, #207).
--              Feeding this whole file over the console is instead one-char-per-
--              round-trip (~1 min), so this file is really the sim showcase; on
--              hardware prefer the resident ledshow().
--
-- Colours are 0..255 (gamma-corrected); nab.fade runs in the background (1 ms
-- timer IRQ), nab.delay paces the frames off the same clock.
--
-- REPL note: each line is its own chunk, so every function is one line and state
-- lives in globals.

leds = { "belly", "right", "bottom", "left" }         -- the four belly cones, in ring order
all5 = { "nose", "belly", "left", "right", "bottom" }  -- everything

function clear() for i = 1, #all5 do nab.led8(all5[i], 0, 0, 0) end end
function fade_all(r, g, b, ms) for i = 1, #all5 do nab.fade(all5[i], r, g, b, ms) end end

-- Tiny LCG PRNG (this build is 32-bit-int Lua; the wrap is fine) -> 0..n-1.
rng = 0x2545F491
function rnd(n) rng = (rng * 1103515245 + 12345) & 0x7fffffff; return rng % n end

-- Eight vivid hues to cycle through.
palette = { {255,0,0}, {255,90,0}, {255,200,0}, {0,255,0}, {0,200,120}, {0,120,255}, {80,0,255}, {255,0,140} }

-- 1) Rainbow: crossfade ALL LEDs smoothly through the palette.
function rainbow(ms) for i = 1, #palette do c = palette[i]; fade_all(c[1], c[2], c[3], ms); nab.delay(ms) end fade_all(0, 0, 0, ms); nab.delay(ms) end

-- 2) Comet: a bright ball runs the belly ring leaving a fading trail; the colour
--    changes each lap.
function comet(laps) for k = 1, laps do c = palette[((k - 1) % #palette) + 1]; for i = 1, #leds do nab.fade(leds[i], c[1], c[2], c[3], 80); nab.delay(150); nab.fade(leds[i], 0, 0, 0, 430) end end nab.delay(300) end

-- 3) Police: snappy red/blue alternation, nose against the belly ring.
function police(n) for k = 1, n do clear(); nab.led8("nose", 255, 0, 0); nab.delay(110); for i = 1, #leds do nab.led8(leds[i], 0, 0, 255) end nab.led8("nose", 0, 0, 0); nab.delay(110) end clear() end

-- 4) Sparkle: random LEDs twinkle in random colours and fade out.
function sparkle(n) for k = 1, n do w = all5[rnd(5) + 1]; c = palette[rnd(#palette) + 1]; nab.fade(w, c[1], c[2], c[3], 100); nab.delay(85); nab.fade(w, 0, 0, 0, 500) end nab.delay(500) end

-- 5) Wipe: a colour sweeps across nose then around the ring, each LED trailing
--    the last, then all fade out together.
function wipe(r, g, b, ms) nab.fade("nose", r, g, b, ms); for i = 1, #leds do nab.fade(leds[i], r, g, b, ms); nab.delay(ms // 2) end nab.delay(ms); fade_all(0, 0, 0, ms); nab.delay(ms) end

function demo() clear(); nab.delay(200); print("rainbow"); rainbow(450); print("comet"); comet(8); print("police"); police(6); print("sparkle"); sparkle(16); print("wipe"); wipe(0, 180, 255, 550); wipe(255, 70, 0, 550); fade_all(0, 0, 0, 400); nab.delay(600) end

-- Run once. On hardware, wrap in a loop for a continuous show:  while true do demo() end
demo()
print("led-demo done")
