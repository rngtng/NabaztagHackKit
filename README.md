# Nabaztag Hack Kit

A complete MTL firmware and SDK for the Nabaztag v1/v2 IoT rabbit — including a dockerized toolchain, a reusable MTL standard library, a Forth interpreter, and a full application stack for WiFi, audio, RFID, LEDs, and ears.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Getting Started

The only host requirements are [Docker](https://www.docker.com/) and [Task](https://taskfile.dev). Every build runs inside Docker — no toolchain to install locally.

```
task
```

Run with no args to list all targets.

### Compile & Simulate

Compile an MTL source to bytecode:

```
task mtl:compile SOURCE=src/app/main.mtl
```

Run in the simulator:

```
task mtl:simulate SOURCE=src/app/main.mtl
```

Run the test suite:

```
task test
```

---

## Architecture

The project is split into three layers: a reusable standard library (`lib/`), a boot image (`src/boot/`), and the main application (`src/app/`). The layers are intentionally kept separate so that `lib/` has no knowledge of hardware specifics and `src/app/` assembles the final product.

```
┌─────────────────────────────────────────────────────────┐
│  src/app/          Application layer                    │
│  ─ main.mtl        Entry point, task scheduler          │
│  ─ hw/             LEDs, ears, button, RFID             │
│  ─ net/            WiFi, DHCP, DNS, NTP, HTTP client    │
│  ─ forth/          Forth interpreter (app extension)    │
│  ─ srv/            Telnet server, HTTP server           │
│  ─ chor/           Choreography (audio/visual effects)  │
│  ─ audio/          MP3, recording, MIDI                 │
├─────────────────────────────────────────────────────────┤
│  src/boot/         Boot image                           │
│  ─ config server   WiFi provisioning UI                 │
│  ─ firmware        OTA firmware upgrade                 │
├─────────────────────────────────────────────────────────┤
│  lib/              Standard library (no hardware deps)  │
│  Layer 4: Forth interpreter                             │
│  Layer 3: Network  (sock, http_server)                  │
│  Layer 2: Encoding (b64, url, json, md5, net)           │
│  Layer 1: Types    (protos/)                            │
│  Layer 0: Utilities (integer, string, list)             │
└─────────────────────────────────────────────────────────┘
```

### lib/ — Standard Library

Each layer depends only on layers below it. The dependency rule is strict: nothing in `lib/` may reference hardware, WiFi, or app-specific state.

| Layer | Modules | Dependencies |
|-------|---------|--------------|
| 0 — Utilities | `integer.mtl`, `string.mtl`, `list.mtl` | none (MTL built-ins only) |
| 1 — Types | `protos/sock_protos.mtl`, `protos/forth_protos.mtl`, `protos/word_protos.mtl`, `protos/ascii_protos.mtl` | none |
| 2 — Encoding | `b64.mtl`, `url.mtl`, `json.mtl`, `net.mtl`, `md5.mtl` | Layer 0 |
| 3 — Network | `sock.mtl`, `http_server.mtl` | Layers 0–2 |
| 4 — Forth | `forth.mtl` + `forth/*.mtl` | Layers 0–2 only |

`lib/forth` sits at Layer 4 and does **not** depend on `lib/sock`. I/O is wired by the caller through two callbacks stored in the interpreter state:

- `write_fn` — output callback; called by the interpreter whenever it needs to write a string. Set to `nil` to buffer output in `f.output` instead.
- `readline_cb` — pending-read callback; set by `READ-LINE` when the interpreter suspends waiting for a line. The transport layer delivers input by calling `f.readline_cb input` and clears this field.

This keeps the interpreter transport-agnostic. A socket-backed REPL, an HTTP-triggered script, and a task-spawned evaluation all use the same interpreter code with different write functions.

#### Wiring I/O at the app layer

```mtl
// telnet REPL: wire sock_send as the write function
let sock_create cnx nil -> sock in
let forth_interpreter_setup nil nil (fixarg2 #sock_send sock) nil nil -> f in
    forth_interpreter_ex text f (fixarg2 #sock_send sock) nil cb;;

// background task: no I/O → output buffered in f.output
forth_interpreter_ex text nil nil task nil;;
```

#### sock.mtl API

| Function | Description |
|----------|-------------|
| `sock_create cnx callback` | Create a new Sock (use this instead of naming fields inline) |
| `sock_send sock s` | Append `s` to output buffer and flush |
| `sock_write sock` | Flush the output buffer to the TCP connection |
| `sock_close_after sock` | Mark for close; closes immediately if buffer is empty |
| `sock_send_and_close sock s` | Send `s` then close |

#### http_server.mtl API

| Function | Description |
|----------|-------------|
| `starthttpsrv port cbrequest` | Listen on `port`; call `cbrequest(rawRequest)` → HTML string |
| `tcpsend sock s` | Send response body fragment |
| `tcpcloseafter sock` | Signal end of response |

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

---

## Directory Layout

```
lib/                 Reusable MTL standard library
lib/protos/          Type definitions (no logic)
lib/forth/           Forth interpreter core
src/app/             Main application
src/app/hw/          Hardware drivers (LEDs, ears, button, RFID)
src/app/net/         Network stack (WiFi, DHCP, DNS, NTP, HTTP)
src/app/forth/       Forth interpreter (app extension + dictionary)
src/app/srv/         Servers (telnet, HTTP)
src/app/chor/        Choreography (synchronized audio/LED/ear effects)
src/app/audio/       MP3 playback, recording, MIDI
src/boot/            Boot/provisioning image
src/firmware/        C firmware (VM, HAL, USB, audio)
tools/mtl_linux/     Dockerized MTL compiler and simulator
tools/preprocessor/  C preprocessor for MTL (pcpp-based)
tools/mkfirmware/    Firmware packaging tool
test/                MTL unit tests (lib/ coverage)
docs/                Grammar, commands, hardware notes
build/               Compiled outputs (Nab.hex, Nab.elf)
```

---

## About MTL (Metal)

MTL/Metal is a custom functional language by [Sylvain Huet](http://www.sylvain-huet.com/?lang=en). Files end with `.mtl`. Key features used in this codebase:

- **Types**: `type Sock=[field1 field2];;` — record types; fields accessed as `sock.field`
- **Functions**: `fun name arg1 arg2 = body;;`
- **Partial application**: `fixarg2 #fn val` — fixes argument 2 of `fn` as `val`
- **Lists**: `hd`, `tl`, `::` (cons), `nil`
- **Mutation**: `set field = value;` — in-place field update

Grammar reference: [docs/grammar.md](docs/grammar.md)

---

## Nabaztag Background

- [First-hand info from the creator](http://www.sylvain-huet.com/?lang=en#nabv2)
- [Journal du lapin](https://www.journaldulapin.com/tag/nabaztag/)
- [Protocol overview](http://www.sis.uta.fi/~spi/jnabserver/documentation/index.html)

## Related Projects

- [ccarlo64/firmware_nabaztag](https://github.com/ccarlo64/firmware_nabaztag) — WPA2 firmware
- [RedoXyde/nabgcc](https://github.com/RedoXyde/nabgcc) — GCC-based firmware
- [andreax79/ServerlessNabaztag](https://github.com/andreax79/ServerlessNabaztag)
