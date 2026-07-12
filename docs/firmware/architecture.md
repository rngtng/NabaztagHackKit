# Firmware (`src/firmware/`) — architecture & background

This documents the **Layer 0** of the SDK: the C bytecode VM + drivers that get
flashed to the Nabaztag:tag V2. For the whole-stack picture (VM → MTL → Forth) and
the directory layout, see the [root README](../../README.md); for where the sources
came from, [`PROVENANCE.md`](../../PROVENANCE.md). This doc captures the non-obvious
firmware-specific knowledge — the language stack, the boot model, and the hardware
reference — so it survives the upstream wikis it was gathered from.

## What it is

A bare-metal ARM7TDMI program for the **Oki ML67Q4051** (33 MHz), vendored from the
`nabgcc` GCC port (`arm-none-eabi-gcc`) with WPA2 work on top. It contains the
drivers (`src/firmware/src/hal`), the USB host stack for the RT2501 WiFi dongle
(`src/firmware/src/usb`), the 802.11/crypto layer (`src/firmware/src/net`), and the
**MTL bytecode VM interpreter** (`src/firmware/src/vm`). It has no external build
dependencies — the ARM toolchain builds it on its own.

Build it with `task firmware:build` (see the README for the full target list). The
firmware and the 32-bit MTL toolchain build in **separate Docker images** because the
ARM cross-compiler is unstable under amd64 emulation and must stay in a native image.

## The language stack — MTL is *not* Forth

Three layers, three languages. This is the part that's easy to get wrong:

| Layer | Language | Form | Lives in |
| --- | --- | --- | --- |
| VM / drivers | **C + ARM asm** | native machine code | `src/firmware/` |
| Boot / app | **MTL (Metal)** | bytecode | `src/boot/`, `src/app-*/` |
| Scripting | **Forth** (interpreter written in MTL) | interpreted text | `lib/forth/`, app `vl/*.forth` |

**MTL** ("Metal") is Sylvain Huet's language for Violet — an **ML/Lisp-family
functional language** (`fun f x = expr;;`, algebraic `type T = A | B _`, `;;`
terminators, `nil`, `var`/`const`/`let`). His original source headers label it
`VLISP`; the canonical name and file extension is **MTL**. It compiles to bytecode
for a **stack-machine VM** (`OPdup`/`OPexec`/`OPadd`/…). That stack-based *runtime* is
why "Forth" sometimes gets attached to this platform — but the *source language* is
functional. Forth only appears higher up, as a small interpreter **written in MTL**
that runs user scripts at runtime (see the README's ecosystem notes).

Both the compiler and the VM must agree on the opcode set in
`src/firmware/inc/vm/vbc.h` (152 opcodes) — change the VM's opcodes and the bytecode
must be recompiled; otherwise the two sides are independent.

> The `mtl_linux` simulator VM (`tools/mtl_linux/`) is a **twin copy** of this same C
> VM. A VM bug or fix almost always applies to both — grep the sibling before
> concluding a divergence is intentional, and patch both. (See `CLAUDE.md`.)

## BOOT vs NOMINAL bytecode

The VM runs an MTL program on top. There are two, loaded through the same loader:

- **BOOT** — the embedded boot bytecode, frozen in the firmware image and loaded
  **unconditionally on every power-on** (`src/firmware/src/main.c` → `loaderInit(...)`
  then `interpGo()`). It drives bring-up: WiFi auth, DHCP, the config web server, and
  firmware update. Source is `src/boot/*.mtl` (a modular split of Violet's original
  monolithic `boot.0.0.0.13.mtl`).
- **NOMINAL** — the main application, **downloaded at runtime** over HTTP (per-device,
  not in this repo). The BOOT app fetches it and hands control over via the
  `OPbytecode` opcode (93 / `0x5D`), whose handler is a second `loaderInit` +
  `interpGo` in `src/firmware/src/vm/vinterp.c`.

The push button is a *runtime* input the MTL app polls
(`push_button_value()` in `src/firmware/src/vm/vlog.c`), not a boot-mode selector.

> The embedded BOOT blob must be **bare, unsigned** bytecode (starts `0xf5 0x47`).
> The MTL compiler's sign flag emits an `"amber"` HTTP-delivery wrapper — correct for
> bytecode *served over HTTP*, wrong for the blob `loaderInit` reads directly. Compile
> the embedded boot **unsigned**. (Signing only frames the identical inner bytecode.)

## Two embedding strategies

Same MTL source, two ways it reaches the VM:

| | **Remote-load** (default) | **Monolithic** |
| --- | --- | --- |
| Flashed | C VM + BOOT bytecode only | C VM + app bytecode embedded |
| App delivery | HTTP pull at boot | baked into the firmware image |
| Iterate app | redeploy, no reflash | rebuild + reflash |
| Needs a server | yes (your box) | no — standalone |
| Best for | day-to-day dev, the Forth/REPL loop | recovery, initial flash, server-free demo |

Default to remote-load; keep monolithic as the recovery/initial-flash path.

## Flashing

Both methods write the same native firmware; they differ only in transport.

1. **JTAG (OpenOCD + GDB)** — a physical probe (Raspberry Pi bit-bang or FTDI Bus
   Blaster) wired to the board. Low-level; the only path that can recover a bricked
   device. Full procedure and wiring: [`tools/openocd/README.md`](../../tools/openocd/README.md).
2. **Config page (no probe)** — `task firmware:package` packs `Nab.bin` into an encrypted,
   signed `.sim` (via the Python `tools/mkfirmware/`), which you upload through the
   Nabaztag's own config mode (blue LEDs). No probe, but needs a working bootloader
   already on the device.

To inspect a *running* board (read variables, capture the boot console, back up the
config sector) see [`jtag-debugging.md`](jtag-debugging.md).

## Hardware reference

The board is an **Oki ML67Q4051** (ARM7TDMI). Full chip inventory (WiFi module, MP3
codec, LED driver, RFID coupler, regulators) is in the teardown notes:
[`../hardware-dissection.md`](../hardware-dissection.md). Firmware-relevant facts:

### Memory map

Authoritative from the linker script (`src/firmware/sys/ml67q4051.ld`):

| Region | Origin | Length | Notes |
| --- | --- | --- | --- |
| IntROM (flash) | `0x08000000` | 124k | physically 128k; last 4 KB sector reserved for config data |
| IntRAM | `0x10000000` | 16k | on-chip; holds the stack |
| ExtRAM | `0xD0000000` | 1024k | external 1 MB, the main working RAM |
| ExtROM | `0xC8000000` | 0k | unused |

Flash spans `0x08000000`–`0x08020000` (128k) for `dump` / `flash write_image`.

### Serial console (debug UART)

The firmware's debug console *is* the UART: `consolestr()` → `putst_uart()`
(`src/firmware/src/vm/vlog.c`) — all boot/debug output comes out here.

> ⚠️ **Not available out of the box.** The UART pads are **not populated** from
> factory; a serial line requires the `mod_serial` hardware mod (soldering,
> <https://wk.redox.ws/dev/nab/v2/mod_serial>). Without it, the only console is the
> JTAG `putst_uart` capture (which halts per string and distorts USB timing — see
> [`jtag-debugging.md`](jtag-debugging.md)).

Settings once the mod is done, from `src/firmware/src/hal/uart.c` (`init_uart`, pins
PB0/PB1 in their `TX_RS232`/`RX_RS232` secondary function):

- **115200 baud, 8N1, no flow control.**
- **3.3 V TTL levels** — use a 3.3 V USB-serial adapter or a MAX232 powered at 3.3 V.
  Do **not** wire raw RS-232 voltages to the board.

Hardware tap: a 3-pin header on the right edge of the PCB, below the ventral LED.
Pin 1 (rightmost) is GND; pins 2–3 are TX/RX (the upstream wiki does not pin down
which is which — confirm with a probe before connecting).

## References

- `github.com/RedoXyde/nabgcc` — upstream of the vendored firmware port
- `github.com/andreax79/ServerlessNabaztag` — modern fork; full MTL toolchain + a
  Forth-in-MTL scripting layer
- `wk.redox.ws/dev/nab/start` — upstream author's wiki hub (mostly French); source for
  the JTAG pinout, serial-tap location, and the BOOT/NOMINAL model above. Content is
  at risk (single personal host) — the durable facts are mirrored here.
