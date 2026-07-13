# firmware — the C bytecode VM (Layer 0)

Bare-metal ARM7TDMI firmware for the Oki ML67Q4051: drivers (`src/hal`), USB
host + RT2501 WiFi (`src/usb`), 802.11/crypto (`src/net`), and the MTL
bytecode VM (`src/vm`). Vendored from the `nabgcc` GCC port — see
[`PROVENANCE.md`](../../PROVENANCE.md).

```sh
task mtl:firmware:build      # → mtl/build/firmware/Nab.{elf,hex,bin} (needs boot:build first)
task mtl:firmware:test       # vm smoke + ASan regression guard + crypto KAT
task mtl:firmware:package    # → Nab.sim for upload via the config page
task mtl:firmware:flash      # JTAG flash via the Pi bridge
```

- Architecture, boot model (BOOT vs NOMINAL bytecode), memory map, serial
  console: [`docs/firmware/architecture.md`](../../docs/firmware/architecture.md)
- Flashing / debricking: [`../tools/openocd/README.md`](../tools/openocd/README.md);
  inspecting a live board: [`docs/firmware/jtag-debugging.md`](../../docs/firmware/jtag-debugging.md)
- VM internals + native testing: [`../tools/testvm/README.md`](../tools/testvm/README.md).
  The VM here (`src/vm/`) and the `mtl_linux` simulator VM are **twin copies**
  — patch both (see `CLAUDE.md`).
