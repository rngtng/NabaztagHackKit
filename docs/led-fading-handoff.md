# LED colors & fading — session handoff

Context doc to continue work on branch `claude/led-driver-colors-fading-342d1x`
in a fresh session. Read `CLAUDE.md` first (conventions), then this.

## What this branch contains (5 commits on top of `main`)

Goal: more usable colors and hardware-side fading for the 5 RGB LEDs.
The driver chip is a TLC5922-style 16-channel constant-current sink on SPI
(SPI1 on LLC2_3/4c, SPI0 on LLC2_2): no PWM grayscale — brightness comes from
its 7-bit per-channel dot-correction register (16×7 bits = 14 bytes, MSB
first; channels 0..14 = LED1 R,G,B .. LED5 R,G,B; channel 15 unused).

1. **Gamma table** (`src/firmware/src/hal/led.c` — `convintensity[]`):
   gamma 2.2, 8→7 bit. Old table mapped inputs 0..51 to 0 (fades snapped to
   black); new one maps every non-zero input to ≥1 and uses the full 0..127.
2. **Fade engine** (same file + `inc/hal/led.h`, hook in `src/main.c`):
   - `led_pack(chan, val7)` — generic packer into the `led_intensity[14]`
     shadow register; replaced the old per-LED mask soup. Verified
     bit-identical against the old code over 200k random writes (host test).
   - `led_flush()` — single 14-byte SPI shift-out, shared by all writers.
   - `led_fade(led, color, ms)` — linear interpolation from the LED's current
     8-bit color (`led_cur[5][3]`, pre-gamma) to a target.
   - `led_fade_tick()` — advances all fades, one flush for all LEDs; called
     from the main loop, self-rate-limited to `LED_FADE_TICK_MS` (10 ms) via
     `counter_timer` (1 ms IRQ tick). Everything stays in main-loop context —
     no new concurrency.
   - `set_led()` keeps its exact old semantics and cancels a running fade.
3. **`ledfade` MTL builtin, opcode 153** (`fun[I I I]I`, returns the led index):
   - firmware VM: `inc/vm/vbc.h`, `inc/vm/vbc_str.h`, `inc/vm/vlog.h`,
     `src/vm/vinterp.c`, `src/vm/vlog.c` (`sysLedFade` → `led_fade`).
   - toolchain `tools/mtl_linux`: same five files, plus the builtin in
     `src/vcomp/stdlib_core.cpp` (NBcore 119→120).
   - opcode 152 is reserved: `strright` exists in the toolchain only; the
     firmware name table carries both entries to stay index-aligned.
   - simulator `sysLedFade` applies the target color instantly (no engine).
4. **Tests**: `test/native/led_test.mtl` (led + ledfade return values, runs in
   simulator via `task test`).
5. **Docs**: `CHANGELOG.md` v0.4.0, `PROVENANCE.md` (new — vendored origins +
   local changes), TODO items 5–7 (see below).

## How to verify (all in Docker; only host deps are Docker + Task)

```
task test              # MTL unit tests in simulator — expect every assert "is valid"
task build:boot        # preprocess + compile boot.mtl → build/boot/dumpbc.c
task build:firmware    # dumpbc.c → src/bc.c → ARM build → build/firmware/Nab.{bin,elf,hex}
```

All three passed on this branch (2026-07-04). Not yet done: flash to a real
rabbit and eyeball a fade (`ledfade 0 0x7F0000 2000`) — JTAG/config-page
upload per `src/firmware/README`.

## Where to continue (TODO.md items 5–7)

- **Temporal dithering** (TODO 5): fade engine keeps 16-bit brightness and
  alternates adjacent 7-bit codes per tick → ~9–10 effective bits for slow dim
  fades. Natural place: `led_fade_tick`/`led_apply` in `led.c`.
- **Centralize green/blue swap** (TODO 6): LLC2_2 swaps G/B channels; today
  only the `RGB_GREEN`/`RGB_BLUE` defines in `led.h` handle it, raw hex colors
  bypass it. Fold into `set_led`/`led_fade` channel extraction.
- **Simulator fade** (TODO 7): animate `ledfade` in `mtl_simu` once it has a
  tick/UI (TODO 4); today it jumps to the target color.
- App layer: `src/app-piper/hw/leds.mtl` still does breathing by recomputing
  colors per task tick (`_leds_osc`) — could be simplified to `ledfade` calls,
  but app-piper is upstream's; consider backporting via `PROVENANCE.md` notes.

## Gotchas discovered this session

- Fade interpolation is linear in pre-gamma 8-bit space; gamma applies at pack
  time. Don't interpolate post-gamma values.
- `set_led_rgb()` is the raw 7-bit legacy API (no gamma); it syncs the 8-bit
  fade state approximately (`v<<1|v>>6`). No in-tree callers besides `led.c`.
- The `mtl_linux` compiler arity/type tables in `stdlib_core.cpp` are four
  parallel arrays + a manual `NBcore` count — keep all five in sync.
- The remote sandbox needed the agent-proxy CA injected to build the
  preprocessor image (pip TLS). Repo Dockerfiles are correct as-is; on a
  normal host `task` builds them unchanged.
