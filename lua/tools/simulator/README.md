# simulator — Unicorn instruction-level simulator (#96)

Runs a `lua/firmware` ELF on the host with no JTAG and no device: maps the
real memory regions, starts from `Reset_Handler`, stubs peripheral pages, and
implements the semihosting console. Driven through the firmware's tasks:

```sh
task lua:firmware:simulate [APP=…] [ARGS=…]   # run an app, report reaching main
task lua:firmware:repl [SCRIPT=…]             # drive the Lua REPL via semihosting
```

What it does and doesn't model (no timing/audio/WiFi/RFID; DREQ/ADC
unmodeled): see [`lua/firmware/README.md`](../../firmware/README.md#simulate-no-hardware).
`simulate.py` holds the machine model; the Docker image bundles Unicorn.
