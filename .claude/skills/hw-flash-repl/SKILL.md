---
name: hw-flash-repl
description: Flashing a lua-track firmware app to real Nabaztag:tag hardware over JTAG, then reading and driving its UART serial console on the Raspberry Pi rig. Use whenever the task involves the Raspberry Pi JTAG rig (jtag.local), openocd, the UART serial console, or verifying a driver/binding against actual hardware rather than the simulator.
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

## The UART console (input + output, #203)

There is one console: **UART0, both directions, @38400 8N1** on OKI `PB0` (TX)
and `PB1` (RX), wired to the Pi's `/dev/serial0`. No OpenOCD session and never
halts the CPU - use it for the REPL, probe output, and anything timing-sensitive
(USB/WiFi). Read/driven via `task lua:firmware:repl:hw`.

Raw manual read on the Pi (when you want the stream without the REPL driver):
`sudo systemctl stop serial-getty@ttyAMA0` (deliberately not `disable`d - do
this every session, re-enables on reboot; #203), then
`sudo stty -F /dev/serial0 38400 raw -echo; sudo cat /dev/serial0`. Link sanity
check: `task lua:firmware:flash APP=uartprobe` -> a repeating
`NAB-UART-PROBE alive @38400 8N1` banner. Dead line without a scope:
`sudo pinctrl set <gpio> ip pd; pinctrl get <gpio>` - driven reads `hi`,
floating reads `lo` (how a TX/RX swap shows up). Not 115200: the UART clock
is a measured 8 MHz (`lua/firmware/inc/hal/uart.h`).

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

## Reading / driving the UART console (`repl:hw`)

`task lua:firmware:repl:hw [APP=lua] [SCRIPT=path.lua] [PI_HOST=…]` flashes the app
over JTAG, then drives `/dev/serial0` on the Pi. The firmware is **parser-less**
(#128) - it runs only `luac` bytecode - so all input is compiled off-device
(`tools/luac`). **No SCRIPT** = a live interactive prompt: `luash.py` compiles each
line you type and drives `uart_repl.py --relay`; Ctrl-D ends it. **SCRIPT=file** =
a scripted run: `replpipe.py` frames the `.lua`/`.lc` file, `flash.py --uart` feeds
it. (There is no `LC` flag any more; framing is implicit. Source typed at a bare
`screen`/`cat` will NOT run.) Input is fed **paced, byte-by-byte** - the OKI UART0
has only a 16-byte RX FIFO and no HW flow control, so bursting a whole line drops
bytes; the driver spaces each char. A scripted run is terminated with **EOT
(`0x04`)**; the app prints `<<FV_DONE>>` when the REPL hits EOF, and `repl:hw`
stops on that marker (backstop: `--run-timeout`, 120s default). `lua.elf` boots
straight to the `> ` prompt - type `run()` for the RFID demo.

If you write a new probe app, call `init_uart()` first, print via `putst_uart`,
and emit `<<FV_DONE>>` before it idles.

**Probe retry loops must fit `repl:hw`'s read window.** The UART read is capped
by `--run-timeout` (120s default), and each failed I2C/SPI call that spins a
1M-iteration timeout loop burns real seconds - keep probe retries ≤ 5, never
100+. Production `writecheck`/`readcheck` (1000 retries) is fine in the real app
where no cap applies; probes need a reduced copy. And **re-run once before
concluding unexpected output is a bug** - one re-run rules out timing flukes
(tag not yet in position, bus state left over from the prior flash) before you
spend a round-trip on a diagnostic probe.

## Concurrency

**Only one OpenOCD may own the Pi/JTAG at a time.** Launching a second
`flash`/`repl:hw` while one is running fails with
`Error: couldn't bind to socket: Address already in use`. Wait for the prior
background run to finish before starting the next - don't retry blind.

## When a binding fails on hardware

Instrument the *real* failing path in situ (a few `putst_uart`/readbacks inside
the actual binding) - do not build an ever-closer standalone probe replica to
debug against. The replica tends to diverge from the real path in some small
but load-bearing way (e.g. a missing init call) and sends you chasing the
wrong theory. Also read the source before iterating: hardware round-trips are
expensive (flash ~13s, per-char UART TX), a source read is not - this
applies to bus/peripheral semantics (SPI FIFO, DREQ, chip-select) as much as
to the flash mechanics above. The patched openocd sources live on
the Pi at `~/nabgcc/openocd-0.8.0/src` - a flash/JTAG hunch is cheap to
confirm there before burning a round-trip. (M8/#116 burned ~7 round-trips on a
silent beep whose cause - SPI0 RX-FIFO never drained by `WriteSPI`, DREQ dips
after SCI writes - was readable in `lua/firmware/src/hal/audio.c` + `spi.c` up front.)

## When a probe works but the full app doesn't

The *difference* between them is the bug - isolate it, don't tweak the app
by-ear. Take the working minimal probe and add the app's factors back one at a
time (init order, ExtRAM heap, UART I/O), reading objective status at
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

`putch_uart` is polled-blocking: it spins on THR-empty for ~0.26 ms per char
at 38400 8N1. A `putst_uart`/`DBG_*` inside an ISR (USB rx callbacks, timer
handlers) holds the CPU in that spin for the whole string, starving every
other interrupt (the UART rx IRQ, the tick, the OHCI ISR) for that time -
stretching `DelayMs`, timing out in-flight USB transfers, and wedging runs in
ways that vanish when you remove the print (#119 scan-callback lesson). Record
data in a buffer inside the ISR; print from the main loop. Debug builds
(`DEBUG=1`) trace from ISRs by design - expect them to perturb timing badly and
use a longer `--run-timeout`.
