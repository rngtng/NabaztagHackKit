# Nabaztag Hack Kit

A complete MTL firmware and SDK for the Nabaztag v1/v2 IoT rabbit — including a dockerized toolchain, a reusable MTL standard library, a Forth interpreter, and a full application stack for WiFi, audio, RFID, LEDs, and ears.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Getting Started

The only host requirements are [Docker](https://www.docker.com/) and [Task](https://taskfile.dev). Every build runs inside Docker — no toolchain to install locally.

## Architecture

The project is split into three components: a toolchain and build system (`tools/`), a reusable standard library (`lib/`), and applications which integrates those. These are either the initial boot image (`src/boot/`), or main applications (`src/app-**`) loaded from remote.

```
 ┌─ Layer 3  Forth scripts (vl/*.forth)        edit-at-runtime, REPL      ← nabaztag-piper/vl
 │              ▲ interpreted by
 ├─ Layer 2  MTL app  (src/app-**/*.mtl)       incl. a Forth interpreter  ← nabaztag-piper/firmware
 │           + MTL stdlib (lib/*.mtl)          written in MTL             ← mtl_library/lib
 │              ▲ compiled to bytecode by
 │           MTL toolchain (mtl_comp/simu)     host-side                  ← nabgcc(mtl_linux)
 │              ▲ remotely loaded by
 ├─ Layer 1  MTL Boot app  (src/boot/*.mtl)    written in MTL             ← mtl_library/lib
 │              ▲ compiled to bytecode by
 │           MTL toolchain (mtl_comp/simu)     host-side                  ← nabgcc(mtl_linux)
 │              ▲ is packed with and runs on
 └─ Layer 0  C bytecode VM + drivers + WPA2    bare-metal ARM7TDMI        ← nabgcc  (origin: firmware_nabaztag)
              flashed once to Oki ML67Q4051
```


### Directory Layout

```
docs/                Grammar, commands, hardware notes
lib/                 Reusable MTL standard library
src/app-*            Various applications
src/boot/            Boot/provisioning image
src/firmware/        C firmware (VM, HAL, USB, audio)
tools/mtl_linux/     Dockerized MTL compiler and simulator
tools/preprocessor/  DockerizedC preprocessor for MTL (pcpp-based)
tools/mkfirmware/    Dockerized Firmware packaging tool
test/                MTL unit tests (lib/ coverage)
CHANGELOG.md         SDK-side changes, tagged by source area, so a diff can be cherry-picked back
```

## Toolchain and build system - tools/

Run `task -a` to list all targets.

### Compile & Simulate

Compile an MTL boot source to bytecode:

```
task build:boot
```

Run the boot app in the simulator:

```
task simulate:boot
```

Finally, compile the firmware sources to bytecode,including the boot code:

```
task build:firmware
```

Run the lib test suite:

```
task test
```

### Standard Library - lib/

Each layer depends only on layers below it. The dependency rule is strict: nothing in `lib/` may reference hardware, WiFi, or app-specific state.

| Layer | Modules | Dependencies |
|-------|---------|--------------|
| 0 — Utilities | `integer.mtl`, `string.mtl`, `list.mtl` | none (MTL built-ins only) |
| 1 — Types | `protos/sock_protos.mtl`, `protos/forth_protos.mtl`, `protos/word_protos.mtl`, `protos/ascii_protos.mtl` | none |
| 2 — Encoding | `b64.mtl`, `url.mtl`, `json.mtl`, `net.mtl`, `md5.mtl` | Layer 0 |
| 3 — Network | `sock.mtl`, `http_server.mtl` | Layers 0–2 |
| 4 — Hardware | tbd. | tbd. |
| 5 — Forth | `forth.mtl` + `forth/*.mtl` | Layers 0–2 only |


- `lib/forth` sits at Layer 5  I/O is wired by the caller through two callbacks stored in the interpreter state:
- `write_fn` — output callback; called by the interpreter whenever it needs to write a string. Set to `nil` to buffer output in `f.output` instead.
- `readline_cb` — pending-read callback; set by `READ-LINE` when the interpreter suspends waiting for a line. The transport layer delivers input by calling `f.readline_cb input` and clears this field.

This keeps the interpreter transport-agnostic. A socket-backed REPL, an HTTP-triggered script, and a task-spawned evaluation all use the same interpreter code with different write functions.


### src/app/ — Application

The application includes `lib/forth` then extends it with hardware-specific Forth words. App code assembles the layers at startup:

```
src/app/forth/forth.mtl
  #include lib/forth          ← generic interpreter
  #include forth/dictionary   ← hardware words (say, ears, leds, rfid, …)
  #include forth/net          ← HTTP fetch, Forth-level DNS
  #include forth/task         ← async task management
```

The telnet server starts a Forth REPL per connection. The HTTP server evaluates Forth code sent via POST. Both share the same interpreter; only the write function differs.

```
src/app/srv/
  telnet_server.mtl    ← REPL over TCP; READ-LINE supported
  http_server.mtl      ← REST API + Forth evaluation
```


### src/boot/ — Boot Image

The boot image handles WiFi provisioning (serves a captive-portal config page) and OTA firmware upgrades. It does **not** include `lib/` — it has its own minimal HTTP and TCP stack to keep the flash footprint small.

┌─────────────────────────────────────────────────────────┐
│  src/boot/         Boot image                           │
│  ─ config server   WiFi provisioning UI                 │
│  ─ firmware        OTA firmware upgrade                 │
└─────────────────────────────────────────────────────────┘

## Nabaztag Background

- [First-hand info from the creator](http://www.sylvain-huet.com/?lang=en#nabv2)
- [Journal du lapin](https://www.journaldulapin.com/tag/nabaztag/)
- [Protocol overview](http://www.sis.uta.fi/~spi/jnabserver/documentation/index.html)

## Related Projects

- [ccarlo64/firmware_nabaztag](https://github.com/ccarlo64/firmware_nabaztag) — WPA2 firmware
- [RedoXyde/nabgcc](https://github.com/RedoXyde/nabgcc) — GCC-based firmware
- [andreax79/ServerlessNabaztag](https://github.com/andreax79/ServerlessNabaztag)
