---
name: hw-flash-repl
description: Flashing a lua-track firmware app to real Nabaztag:tag hardware over JTAG, or reading its console (UART or semihosting REPL). Use whenever the task involves the Raspberry Pi JTAG rig (jtag.local), openocd, the UART serial console, semihosting output, or verifying a driver/binding against actual hardware rather than the simulator.
---

# Hardware flash + console (lua firmware, JTAG + UART)

Full recipe and troubleshooting: `lua/tools/openocd/README.md`. Board teardown /
chip inventory: `docs/hardware-dissection.md`.

## Always use the taskified path

- **Flash**: `task lua:firmware:flash [APP=lua] [PI_HOST=tobi@jtag.local]`
- **Read console / REPL**: `task lua:firmware:repl:hw [APP=lua] [SCRIPT=path.lua] [PI_HOST=tobi@jtag.local]`

Never hand-roll `scp` + `openocd` + `gdb` - the raw ssh+openocd path is denied,
and both operations are already taskified. If a task doesn't cover what you
need, extend the task rather than shelling out around it.

## Two consoles - pick by whether you need input

- **UART (output only, preferred, #203):** UART0 TX @38400 8N1 on PB0, wired
  to the Pi's `/dev/serial0`. No OpenOCD session, never halts the CPU - use it
  for probe output and anything timing-sensitive (USB/WiFi). Read it on the Pi:
  `sudo systemctl stop serial-getty@ttyAMA0` (re-enables on reboot), then
  `sudo stty -F /dev/serial0 38400 raw -echo; sudo cat /dev/serial0`. Link sanity
  check: `task lua:firmware:flash APP=uartprobe` -> a repeating
  `NAB-UART-PROBE alive @38400 8N1` banner. Dead line without a scope:
  `sudo pinctrl set <gpio> ip pd; pinctrl get <gpio>` - driven reads `hi`,
  floating reads `lo` (how a TX/RX swap shows up). Not 115200: the UART clock
  is a measured 8 MHz (`lua/firmware/inc/hal/uart.h`).
- **Semihosting (input + output, legacy):** the only path with *input*
  (`SYS_READC`), so the interactive REPL and `task lua:firmware:repl:hw` still
  run on it; Lua `print()` is also still wired to it. Every char is a debugger
  trap that halts the whole CPU - everything below about breakpoints, the
  120s cap, `<<FV_DONE>>` and IRQ printing is semihosting-specific.

## Before writing any driver or binding

**Confirm the peripheral actually exists on this board first.** A wired
chip-select pin is not proof of a populated chip - read
`docs/hardware-dissection.md`, and if the rig is up, probe the device id over
the bus *before* building anything. (M6/#94 built a full AT45 flash driver
against a `CS_FLASH` pin the board doesn't populate, then had to revert.) The
cheapest disqualifying test comes first, not last.

## Sanity signal

IDCODE `0x3f0f0f0f` on JTAG connect = CPU alive, wiring good. If you don't see
this, stop and debug the physical connection before anything else.

## Semihosting console gotcha (M3+)

The ARM7TDMI EmbeddedICE on this board is v1 with no vector-catch, so
openocd's default auto-breakpoint at the SWI vector `0x8` fails (flash is
read-only there). The fix is a **hardware breakpoint at `0x8`** + `rbp 0x8`,
set *after* the final `reset halt` (reset wipes the ICE watchpoint regs).
`task lua:firmware:repl:hw` already does this - you only need to know it when
debugging why a manual openocd session hangs at the SWI vector. (Cleaner fix
still TODO: patch openocd `arm7_9_setup_semihosting` to use `BKPT_HARD`.)

**The semihosting HW-bp fires on ANY SWI, not just `svc #0xAB`.** Any firmware
code using `swi N` for non-semihosting purposes (e.g. OKI IRQ dis/enable via
the SWI dispatch table) halts OpenOCD without resuming - output just stops
after the last print before that SWI, no error. Fix: replace SWI-based helpers
with ARM-mode functions (`__attribute__((target("arm")))`) that manipulate
CPSR directly (`mrs`/`msr cpsr_c`), bypassing the SWI vector. See the M9 fix
in `lua/firmware/sys/src/irq.c` (#117).

## Reading the semihosting console (`repl:hw`)

Semihosting apps print `<<FV_DONE>>` when finished (REPL after input EOF, a
probe before it idles). `flash.py` streams console output live and exits on
that marker instead of waiting the full `--run-timeout` (~120s → seconds).
If you write a new probe app, emit `<<FV_DONE>>` before it idles.

**Probe retry loops must fit the 120s semihosting cap.** Each failed I2C/SPI
call that spins a 1M-iteration timeout loop takes ~3-6s on ARM7TDMI @32 MHz -
keep probe retries ≤ 5, never 100+. Production `writecheck`/`readcheck` (1000
retries) is fine in the real app where no cap applies; probes need a reduced
copy. And **re-run once before concluding unexpected output is a bug** - one
re-run rules out timing flukes (tag not yet in position, bus state left over
from the prior flash) before you spend a round-trip on a diagnostic probe.

## Concurrency

**Only one OpenOCD may own the Pi/JTAG at a time.** Launching a second
`flash`/`repl:hw` while one is running fails with
`Error: couldn't bind to socket: Address already in use`. Wait for the prior
background run to finish before starting the next - don't retry blind.

## When a binding fails on hardware

Instrument the *real* failing path in situ (a few `sh_puts`/readbacks inside
the actual binding) - do not build an ever-closer standalone probe replica to
debug against. The replica tends to diverge from the real path in some small
but load-bearing way (e.g. a missing init call) and sends you chasing the
wrong theory. Also read the source before iterating: hardware round-trips are
expensive (flash ~13s, per-char semihosting), a source read is not - this
applies to bus/peripheral semantics (SPI FIFO, DREQ, chip-select) as much as
to the semihosting/flash mechanics above. The patched openocd sources live on
the Pi at `~/nabgcc/openocd-0.8.0/src` - a semihosting/flash hunch is cheap to
confirm there before burning a round-trip. (M8/#116 burned ~7 round-trips on a
silent beep whose cause - SPI0 RX-FIFO never drained by `WriteSPI`, DREQ dips
after SCI writes - was readable in `lua/firmware/src/hal/audio.c` + `spi.c` up front.)

## When a probe works but the full app doesn't

The *difference* between them is the bug - isolate it, don't tweak the app
by-ear. Take the working minimal probe and add the app's factors back one at a
time (init order, ExtRAM heap, semihosting I/O), reading objective status at
each step. #123 wasted ~15 by-ear round-trips guessing one VS1003 register at
a time; the fix came only once `volprobe` re-added `init_hw`, then a
`SYS_READC` burst, then an ExtRAM write burst with `CLOCKF`/`HDAT1` readbacks -
isolating that **ExtRAM/EMC access corrupts SPI0 SCI**, which the Lua heap (in
ExtRAM) triggers constantly.

## Debugging discipline

- **Prefer objective on-device signals over by-ear/by-eye.** Every #123
  breakthrough was a register/`HDAT1`/DREQ readback; the ear was slow, needed
  the user, and even gave self-contradictory results.
- **Checkpoint after ~3 failed same-symptom round-trips.** Stop and write
  knowns/unknowns/next-experiment rather than continuing to guess.
- **Get the peripheral datasheet, and diff the mtl firmware (`mtl/firmware`,
  the working oracle), before guessing a chip's registers.** Most #123 dead-ends -
  `SM_SDISHARE` (shared-chip-select decode mode), VS1003 decodes MP3 not raw
  PCM WAV, endFillByte, `CLOCKF` encoding + post-reset timing - were plain
  datasheet facts, guessed one costly round-trip at a time.
- **Before diagnosing any lua-firmware driver, grep `lua/firmware/inc/common.h` for
  `#define` compile-time switches** (e.g. `MOTOR_SPEED_CONTROL`, `DRIVER_ST`).
  They flip entire code paths - M10/#118 wasted 2 hardware rounds diagnosing
  GPIO when `MOTOR_SPEED_CONTROL` had already switched motor drive to the FTM
  PWM peripheral. `FTMEN=0x3F` (vs `0x0F`) is the quick on-device tell that
  `MOTOR_SPEED_CONTROL` is active.

## Never print from IRQ context

Each semihosting char is a debugger trap: the HW breakpoint at `0x8` halts
the **whole CPU** while OpenOCD services it. A `sh_puts`/`DBG_*` inside an
ISR (USB rx callbacks, timer handlers) starves every other interrupt for
milliseconds per character - stretching `DelayMs`, timing out in-flight USB
transfers, and wedging runs in ways that vanish when you remove the print
(#119 scan-callback lesson). Record data in a buffer inside the ISR; print
from the main loop. Debug builds (`DEBUG=1`) trace from ISRs by design -
expect them to perturb timing badly and use a longer `--run-timeout`.
(UART `putch_uart` is polled-blocking too - ~0.3 ms/char at 38400 - far
cheaper than a semihosting trap, but the buffer-in-ISR/print-in-main rule
still applies.)
