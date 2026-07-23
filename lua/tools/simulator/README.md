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

What it does and doesn't model (no timing/audio/WiFi; DREQ/ADC unmodeled): see
[`lua/firmware/README.md`](../../firmware/README.md#simulate-no-hardware).
`simulate.py` holds the machine model; the Docker image bundles Unicorn.

## Peripheral I/O + control protocol (#42)

The sim observes the output peripherals (the five RGB LEDs, reconstructed from
the SPI1 frame — see `--leds`) and models the **input** peripherals so scripts
and a future UI (#43) can drive them and watch device state:

* **button** — `nab.button()` reads PI3 bit1; the sim forces it from injected state.
* **RFID** — `nab.rfid()` returns the injected UID (or `nil`); the real
  `rfid_read_uid` runs on the stubbed I2C and its result is corrected at return
  (no CRX14 bus emulation).
* **ears** — `nab.ear_move`/`ear_stop` set a drive/direction state; a synthetic
  encoder advances while running so `nab.ear_pos()` moves.

Control is **JSON-Lines**, one object per line, dependency-free:

```jsonc
// control in  (--inject-file FILE): each event optionally gated by
//   at_ms (device-ms) and/or after (a console substring); ungated = fire at start
{"after": "prompt", "t": "rfid",   "uid": "d0021a3506198b86"}   // or "uid": null to remove
{"at_ms": 200,      "t": "button", "down": true}
{"t": "ear", "n": 1, "pos": 100}                                 // hard-set an encoder
// state out  (--emit-state [--state-file FILE]): one snapshot per change
{"t":"state","ms":200,"leds":[[r,g,b],…],"ears":[{"dir","run","pos"},…],"button":false,"rfid":null}
```

Run it: `task lua:apps:simulate APP=… INJECT=timeline.jsonl ARGS="--leds --emit-state"`.
The `firmware:test:inject` golden (`task lua:verify`) drives this end to end.
