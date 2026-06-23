# Nabaztag / Nabaztag:tag Hardware Dissection

> Archived knowledge. Preserved here so it survives link rot.
>
> **Source:** Peter Tyser, *"Nabaztag / Nabaztagtag Dissection"* (~2007).
> Original: `http://petertyser.com/nabaztag-nabaztagtag-dissection/` (now dead).
> Archived: <https://web.archive.org/web/20201028193157/http://petertyser.com/nabaztag-nabaztagtag-dissection/>
>
> Text reproduced as-is (including the author's original typos) for fidelity.

---

## Background

The Nabaztag/tag is made by a company called **Violet**. It is Violet's 2nd
generation Nabaztag and adds a few new features on top of the 1st generation.

From the original investigation: it's possible to run bytecode on a "virtual
machine" on the Nabaztag, but that doesn't give full access to the hardware. The
author's goal was to gain complete control — download a custom firmware giving
full control of the processor and peripherals, enabling additional sensors/output
devices and server implementations unassociated with Violet.

The firmware is **not** stored in an easily removable flash chip — it lives in the
ARM processor's internal secured flash (see Analysis below). Investigation
required fully dismantling the device.

---

## Teardown photos (captions)

Images live on archive.org. Prefix each path with the Wayback host
`https://web.archive.org/web/20201028193157im_/` to retrieve.

| File | Caption |
| -- | -- |
| `.../2007/03/800px-Nabaztag-IMG_1666-750x410.jpg` | Stock Nabaztagtag, fresh out of the box. |
| `.../2007/03/assembled-front-225x300.jpg` | Ears and cover removed. Visible: the 4 LED directional cones, microphone at the base, and RFID module (PCB in center with brown, red, orange, and yellow wires). |
| `.../2007/03/chassis-front-197x300.jpg` | View from front with antenna, PCB, and base removed. Grey wire with gold connector links the wireless module to the green PCB (assumed to be an 802.11g antenna). |
| `.../2007/03/chassis-back-200x300.jpg` | View from back with base removed. Large green PCB connects to wireless module. |
| `.../2007/03/base-290x300.jpg` | Base viewed from above. |
| `.../2007/03/antenna-front-300x262.jpg` | Front of RFID module after removal. 13.560 MHz oscillator and STMicro CR14 contactless coupler visible. |
| `.../2007/03/antenna-back-275x300.jpg` | Back of RFID module after removal. |
| `.../2007/03/mainboard-front1-300x210.jpg` | Mainboard front. |
| `.../2007/03/mainboard-back-300x197.jpg` | Mainboard back. |
| `.../2007/03/wireless-300x176.jpg` | Ralink 802.11g wireless USB module using RT2571W chipset. |
| `.../2007/03/oki630eb04j-297x300.jpg` | OKI ML67Q4051 33 MHz ARM7-TDMI processor. |
| `.../2007/03/oki6295b08j-300x292.jpg` | OKI ML60842 USB 2.0 On-the-go controller. |
| `.../2007/03/dram-300x204.jpg` | Samsung K6X8016T3B 1 MByte (512K x 16 bit) CMOS SRAM. |
| `.../2007/03/vlsi-300x284.jpg` | VLSI VS1003B MP3/WMA/WAV/MIDI decoder, ADPCM encoder. |
| `.../2007/03/tlc5922-300x269.jpg` | Texas Instruments TLC5922 16-bit LED driver. |
| `.../2007/03/stcf616-300x241.jpg` | STMicro 393? Dual CMOS Comparator? — used to control ear servos? |
| `.../2007/03/regulator-300x237.jpg` | LD1117L 3.3 V Regulator, STMicro L5973D 2.5 A step-down switching regulator. |
| `.../2007/03/nabaztagtag-157x300.jpg` | (assembled device) |

---

## Component summary

### OKI ML67Q4051 — 33 MHz ARM7-TDMI processor
- 128 KB of "secure" internal flash
- 8 KB "Boot Flash ROM"
- 16 KB internal RAM
- Supports 32-bit instructions or 16-bit thumb instructions
- JTAG interface
- External memory controller
- Peripherals: I2C, SPI, watchdog, RTC, timers, 4 ADCs, DMA controller, 2 UARTs

### OKI ML60842 — USB 2.0 OTG controller
- Used to interface the ARM processor with the Ralink 802.11b/g module

### Samsung K6X8016T3B — 512K x 16 bit CMOS SRAM (1 MB)
- Additional 1 MB of RAM available to the ARM processor

### VLSI VS1003B — MP3/WMA/WAV/MIDI decoder, ADPCM encoder
- Play streamed audio, encode voice commands

### Texas Instruments TLC5922 — 16-bit LED driver
- Drives belly button and nose LEDs
- Serial data interface, SPI compatible

### Ralink RT2571W-based USB 802.11b/g module
- 802.11b/g adapter
- USB interface

### STMicro CR14 — contactless coupler (anti-collision, CRC management)
- Used for RFID
- Supports ISO 14443 type-B protocol
- I2C interface

### Misc
- 13.560 MHz oscillator on the RFID module
- STMicro 393(?) dual CMOS comparator — suspected ear-servo control
- LD1117L 3.3 V regulator
- STMicro L5973D 2.5 A step-down switching regulator

---

## Analysis — firmware extraction difficulty

Violet made it relatively hard to extract the firmware. The 128 KB internal flash
storing the firmware has a security feature. After programming, Violet can enable a
"security mode". With it enabled, external devices (e.g. JTAG or parallel
programmer) can no longer read/write the flash or use standard JTAG debug.

OKI describes the feature:

> When a security bit is set, the contents of Flash ROM cannot be read or rewritten
> in Flash-JTAG mode. A debugging interface (ie JTAG) can no longer be used. The
> security bit can be reset after the chip contents are erased in Flash-JTAG mode or
> parallel programmer mode.

So one could erase the processor's flash and reprogram with a custom image — meaning
the Nabaztag is fairly easily *reprogrammable*. But reprogramming with anything
*useful* is hard because you can't inspect the current firmware first. Without
reverse engineering the stock firmware, porting features like the wireless card
driver would be VERY difficult.

---

## Possible workarounds

All require significant work and may not be possible at all.

### 1. Confirm "security mode" is actually enabled
Assumed enabled to prevent reverse engineering, but unverified — author lacked a
JTAG debugger compatible with the ML67Q4051. First step would be to verify.

### 2. Use the serial programming interface
Configuration pins sampled out of reset can force the processor to execute the Boot
ROM flash (the 8 KB, non-user-reprogrammable flash), allowing the user to erase and
reprogram the 128 KB flash by downloading an image over the serial port — via a
terminal emulator (TeraTerm, minicom) or OKI's own **ISFP** utility.

ISFP can *dump* flash for other OKI chips (ML674001, ML675001), but OKI says this
isn't available on the ML67Q4051. Unclear whether truly disabled in silicon or just
blocked by ISFP software. Risky, but issuing the same serial commands known to work
on the ML674001/ML675001 might work.

### 3. Use "user mode" programming interface to read the flash
Code executing on the ML67Q4051 *can* read the 128 KB flash even with security mode
enabled. If executing code could be compromised, a program could read the flash and
print it to the serial port. Compromising the chip is non-trivial.

### 4. Reverse engineer the firmware published by Violet
Violet provided a firmware image at
`www.nabaztag.com/firmware/firmware.0.0.0.10.sim.txt`. Format/interpretation unclear;
could potentially be reverse engineered into an inspectable image.

### 5. Reprogram a "fresh" image from scratch
Peripheral docs are readily available, plus source for complex parts (VS1003B,
Ralink adapter). In theory one could write firmware from scratch without referencing
the stock firmware — but VERY time consuming.

---

## Conclusion

Reprogramming the Nabaztag is straightforward and low-effort. But *first extracting*
the firmware image to reverse engineer poses significant brick risk and effort.
