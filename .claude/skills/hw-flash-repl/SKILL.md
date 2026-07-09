---
name: hw-flash-repl
description: Flashing a firmwareV2 app to real Nabaztag:tag hardware over JTAG, or reading its console/REPL. Use whenever the task involves the Raspberry Pi JTAG rig (jtag.local), openocd, semihosting console output, or verifying a driver/binding against actual hardware rather than the simulator.
---

# Hardware flash + console (firmwareV2, JTAG)

Full recipe and troubleshooting: `tools/openocd/README.md`. Board teardown /
chip inventory: `docs/hardware-dissection.md`.

## Always use the taskified path

- **Flash**: `task flash:firmwareV2 [APP=hello] [PI_HOST=tobi@jtag.local]`
- **Read console / REPL**: `task repl:firmwareV2:hw [APP=lua] [SCRIPT=path.lua] [PI_HOST=tobi@jtag.local]`

Never hand-roll `scp` + `openocd` + `gdb` — the raw ssh+openocd path is denied,
and both operations are already taskified. If a task doesn't cover what you
need, extend the task rather than shelling out around it.

## Before writing any driver or binding

**Confirm the peripheral actually exists on this board first.** A wired
chip-select pin is not proof of a populated chip — read
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
`task repl:firmwareV2:hw` already does this — you only need to know it when
debugging why a manual openocd session hangs at the SWI vector.

## Reading the console

Semihosting apps print `<<FV_DONE>>` when finished (REPL after input EOF, a
probe before it idles). `flash.py` streams console output live and exits on
that marker instead of waiting the full `--run-timeout` (~120s → seconds).
If you write a new probe app, emit `<<FV_DONE>>` before it idles.

## Concurrency

**Only one OpenOCD may own the Pi/JTAG at a time.** Launching a second
`flash`/`repl:hw` while one is running fails with
`Error: couldn't bind to socket: Address already in use`. Wait for the prior
background run to finish before starting the next — don't retry blind.

## When a binding fails on hardware

Instrument the *real* failing path in situ (a few `sh_puts`/readbacks inside
the actual binding) — do not build an ever-closer standalone probe replica to
debug against. The replica tends to diverge from the real path in some small
but load-bearing way (e.g. a missing init call) and sends you chasing the
wrong theory. Also read the source before iterating: hardware round-trips are
expensive (flash ~13s, per-char semihosting), a source read is not — this
applies to bus/peripheral semantics (SPI FIFO, DREQ, chip-select) as much as
to the semihosting/flash mechanics above.
