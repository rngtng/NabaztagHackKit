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

`simulate.py` holds the machine model; the Docker image bundles Unicorn.

## What it models

* **UART0 console**, both directions — this is how apps and the REPL are fed.
  Each REPL line is its own chunk, so `local`s don't persist; use globals.
* **SPI**, with *instant* completion: a data-register write sets `SPIF`. Enough
  for framing, not for timing.
* **The 1 ms System Timer, including its interrupt** (#102). The CPU runs in
  ~1 ms instruction slices (`INSNS_PER_MS`) and between slices the sim performs
  a real ARM7 IRQ entry (SPSR←CPSR, IRQ mode, vector to the flash-resident
  `0x18` handler) whenever the timer is enabled and interrupts are unmasked.
  The firmware's own dispatcher then runs `timer_handler` → `led_fade_tick` —
  the *actual* fade code — so `counter_timer` advances and **LED fades animate
  in the sim**. The clock is instruction-counted, not cycle-accurate, so
  `nab.time()` is approximate here rather than wall time. An image that never
  enables the timer (most `examples/`) sees no ticks at all.
* **Button / RFID / ear inputs**, injected — see the protocol below.

Not modelled: audio, WiFi, analog. **DREQ (VS1003 ready) and the ADC completion
bit are unmodeled**, so any bounded busy-wait on them (`nab.play`, `nab.wheel`)
spins to its cap and is hardware-only. `nab.record` is the exception — its wait
guard is one-shot, so in-sim it returns a header-only WAV instead of burning the
budget. QEMU isn't used (no ML67Q4051 machine; the memory map doesn't fit).

**Live LED view.** `ARGS=--leds` reconstructs the 14-byte dot-correction frame
from the SPI1 byte stream (latched on the CS_LED rising edge), unpacks it to five
RGB LEDs and draws an ANSI truecolor strip — animated in place on a TTY, one line
per distinct frame when piped. `--speed` (default `1`) paces the sim to wall-clock
so the animation is watchable; lower it (`ARGS="--leds --speed 0.5"`) for a close
look. The run summary reports timer IRQs delivered and LED frames rendered.

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
