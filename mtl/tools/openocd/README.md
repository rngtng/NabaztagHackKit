# Flashing the mtl C firmware over JTAG (OpenOCD)

Flash (or debrick) a Nabaztag:tag (V2) with the mtl track's C VM firmware
(`Nab.elf`). The configs and wiring here mirror
[`lua/tools/openocd/`](../../../lua/tools/openocd/) (per-leaf duplication,
#187); this `flash.py` is the flash-only subset (the mtl C firmware has no
on-device REPL, so it carries no UART-console path). **The full guide lives
there**: [`lua/tools/openocd/README.md`](../../../lua/tools/openocd/README.md)
covers the one-time Raspberry Pi setup (patched OpenOCD 0.8.0 with RedoX's
`ml67q40xx` flash driver), the JTAG wiring table, the manual debrick steps,
and troubleshooting.

Configs in this dir:

| File | Adapter |
|---|---|
| [`nabaztag-pi.cfg`](nabaztag-pi.cfg) | Raspberry Pi GPIO (bit-bang) |
| [`nabaztagv2.cfg`](nabaztagv2.cfg) | FTDI Bus Blaster |
| [`target/ml67q4051.cfg`](target/ml67q4051.cfg) | the chip + 128 KB flash bank (shared by both) |

## Flash in one command

Once the Pi is set up (step 1 + wiring in the lua guide, one-time), flashing
is a single task from the repo root — it builds, ships this repo's configs +
ELF to the Pi, drives OpenOCD + gdb, verifies the write, and tears the bridge
down:

```sh
task mtl:firmware:flash                       # the C VM firmware (Nab.elf)
task mtl:firmware:flash PI_HOST=me@other-pi.local
```

The task wraps [`flash.py`](flash.py). It aborts if the JTAG chain check
(IDCODE `0x3f0f0f0f`) fails, before touching flash.

## Manual fallback

Follow the lua guide's manual steps, substituting the mtl artifact:

```sh
task mtl:firmware:build                    # -> mtl/build/firmware/Nab.{elf,hex,bin}
scp mtl/build/firmware/Nab.elf pi@<pi-ip>:~/openocd/
```

then flash `Nab.elf` with the same gdb `load` sequence. No probe at hand? The
firmware can also be uploaded through the rabbit's own config page — see
[`mtl/tools/mkfirmware/`](../mkfirmware/README.md) (`task mtl:firmware:package`).

To *inspect* a running board over JTAG (read C variables, capture the boot
console, back up the config sector), see
[`docs/firmware/jtag-debugging.md`](../../../docs/firmware/jtag-debugging.md).
