# examples/ — C bring-up probes

Standalone C programs that run **instead of** the Lua firmware, each proving one
subsystem works before anything is built on top of it. None is part of the
product image — `firmware:build` builds one only when `EXAMPLE=` names it.

```sh
task lua:firmware:build EXAMPLE=<name>    # build one
task lua:firmware:flash EXAMPLE=<name>    # build + flash it (see the hw-flash-repl skill)
```

A probe is meant to have no callers — it is a tool you reach for when hardware
misbehaves, not a library. Reach for the **cheapest disqualifying test first**:
confirm the peripheral answers before writing a driver against it.

## The UART ladder

Four probes, because a dead console has four different causes and each isolates
one. Work down the list — this is the order that found the real fault before.

| Probe | Proves | Reach for it when |
|---|---|---|
| `uartprobe` | Physical TX: banner out of PB0 at 115200 8N1, forever | Nothing arrives on the Pi's `/dev/serial0` |
| `uartcal` | Baud: emits 0x55 at a *known* divisor so a scope can measure the real rate | Output arrives but is garbled — the peripheral clock is a measured 8 MHz, not the 33 MHz CPU clock |
| `uartrxprobe` | RX code path, via the 16550's internal loopback — no wire needed | `getch_uart()` is suspect and you cannot yet trust the wiring |
| `uartechoprobe` | Physical RX line: echoes each byte back case-flipped | Loopback passes but typed input still does not arrive |

## Peripheral probes

| Probe | Proves |
|---|---|
| `hello` | The toolchain: builds, links, boots, spins. First thing to try on a new setup |
| `blink` | The nose LED (`LED_RGB_5`) — the first binary that does something visible |
| `ledmap` | Lights every `LED_RGB_*` a distinct colour at once, so a photo maps index → physical LED |
| `earprobe` | Ear motors + encoders respond before trusting the Lua bindings |
| `gpioprobe` | Scans for the wheel's end-of-travel click switch |
| `rfidprobe` | The I2C bus works and the CRX14 coupler answers |
| `rfidprobe2` | Dumps raw CRX14 frame-buffer bytes at each anti-collision step |
| `usbprobe` | The ML60842 OHCI host stack port, end to end |
| `wifiprobe` | RT2501 802.11: USB host + driver bring-up + an AP scan |

## Audio isolation chain

The audio path has several places to fail; these split them apart.

| Probe | Proves |
|---|---|
| `audioprobe` | The VS1003B codec is alive on SPI0 at all |
| `playprobe` | It actually decodes a PCM stream, not just accepts bytes |
| `fmtprobe` | Feeds a real WAV and then a real MP3 — isolates format handling from transport |
| `volprobe` | Plays the embedded MP3 at volume 0 (loudest) — isolates attenuation |
| `recprobe` | ADPCM record bring-up, standalone |

`tones.h` is shared sample data, not a probe.
