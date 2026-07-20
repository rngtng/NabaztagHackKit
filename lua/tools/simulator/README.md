# simulator — Unicorn instruction-level simulator (#96)

Runs a `lua/firmware` ELF on the host with no JTAG and no device: maps the
real memory regions, starts from `Reset_Handler`, stubs peripheral pages, and
models the UART0 console. Driven through the `lua:firmware:simulate` (C firmware
images) and `lua:apps:simulate` (Lua apps) tasks:

```sh
task lua:firmware:simulate [ARGS=…]            # run the product firmware headless, report reaching main
task lua:apps:simulate APP=<lua file> [ARGS=…] # feed a Lua app into the sim
task lua:firmware:simulate:repl                # live interactive Lua REPL over the modelled UART
```

What it does and doesn't model (no timing/audio/WiFi/RFID; DREQ/ADC
unmodeled): see [`lua/firmware/README.md`](../../firmware/README.md#simulate-no-hardware).
`simulate.py` holds the machine model; the Docker image bundles Unicorn.
