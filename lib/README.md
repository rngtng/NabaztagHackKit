# lib — Reusable MTL Library

Portable MTL modules for the Nabaztag firmware. Each file is self-contained and `#include`-able on demand. No module pulls in more than it needs.

The `lib/` modules should declare all external deps as `proto` at the top so each file is self-documenting and linter-friendly.

## About MTL (Metal)

MTL/Metal is a custom functional language by [Sylvain Huet](http://www.sylvain-huet.com/?lang=en). Files end with `.mtl`. Key features used in this codebase:

- **Types**: `type Sock=[field1 field2];;` — record types; fields accessed as `sock.field`
- **Functions**: `fun name arg1 arg2 = body;;`
- **Partial application**: `fixarg2 #fn val` — fixes argument 2 of `fn` as `val`
- **Lists**: `hd`, `tl`, `::` (cons), `nil`
- **Mutation**: `set field = value;` — in-place field update

Grammar reference: [docs/grammar.md](docs/grammar.md)

## Architecture

```
lib/
├── protos/          Shared types & forward declarations
│   ├── sock_protos.mtl   Sock type — shared by http_server & sse_server
│   ├── sse_protos.mtl    SSE public API protos
│   ├── forth_protos.mtl  Word/Forth types + interpreter protos
│   ├── task_protos.mtl   Task/TaskStatus types + scheduler protos
│   ├── ascii_protos.mtl
│   └── word_protos.mtl
│
├── std/             Pure data primitives (no VM I/O)
│   ├── string.mtl        String helpers (strstr, to_lower, pads, …)
│   ├── integer.mtl       Integer/hex utilities
│   ├── list.mtl          Linked-list helpers (listlen, rev, sort, …)
│   ├── buffer.mtl        Byte-buffer utilities
│   ├── b64.mtl           Base64 encode/decode
│   ├── url.mtl           URL encode/decode
│   ├── json.mtl          JSON string builder
│   ├── xmlparser.mtl     XML parser
│   ├── md5.mtl           MD5 hash
│   └── net.mtl           IP/MAC address conversions
│
├── sys/             Runtime glue
│   ├── task.mtl          Cooperative task scheduler (task_start/task_scheduler)
│   ├── time.mtl          NTP-backed clock, date formatting/parsing
│   ├── timezones.mtl     City-code → UTC-offset table
│   ├── firmware.mtl      Firmware image helpers
│   ├── bytecode.mtl      OTA bytecode ("amber") parsing/loading
│   ├── system.mtl        System/OS helpers
│   └── echo.mtl          Debug output helpers (dumps, MAC/IP echo)
│
├── net/             Networking on VM-native TCP
│   ├── tcp.mtl           VM adapter: tcpSend/tcpListen natives → writetcp/listentcp API
│   ├── sock.mtl          Sock write/close helpers (writetcp/closetcp)
│   ├── http_server.mtl   Single-request HTTP/1.0 server (closes after response)
│   └── sse_server.mtl    Persistent SSE server (keeps connections open)
│
├── hw/              Rabbit hardware on VM natives
│   ├── button.mtl        Click/double/long-click event task (button2)
│   ├── leds.mtl          LED setters, override table, breathing oscillator (led)
│   ├── ears.mtl          Ear motor state machine: reset/goto/detect (motorget/motorset)
│   ├── rfid.mtl          Debounced tag detection (rfidGet) — rfid_poll
│   └── reclib.mtl        Microphone capture → WAV/RIFF (recStart/recStop/recVol)
│
├── forth/           Forth interpreter (sub-modules, assembled by forth.mtl)
│   ├── interpreter.mtl   Tokenizer + interpreter loop
│   ├── compile.mtl       : ; ( constant defined? words
│   ├── dictionary.mtl    forth_core_words + forth_init_core_dictionary
│   ├── word.mtl          Word dict/list accessors
│   ├── json.mtl          JSON parser producing Words + json-parse/json-get
│   ├── stack.mtl / arithmetic.mtl / comparison.mtl / logical.mtl
│   └── control.mtl / list.mtl / string.mtl / output.mtl
│
└── forth.mtl        Forth entry point — include this, not the sub-modules
```

Future building blocks (extraction from `src/app-piper` pending): `audio/`
(playback/midi), `chor/`, and the `ipv4/` + wifi/dhcp/dns/ntp network stack.

`lib/hw` decouples from app policy via seams: LED *animations* (what blinks
when) stay app-side on top of the lib primitives; the ears state machine
exposes `ears_touched_cb` (user turned an ear — return 1 to consume) and
`ears_post_run_cb` (poll app state each tick); `rfid_poll` returns a fresh
tag id and the app decides the reaction; `reclib` records and packages WAV,
upload flow stays app-side.

**Starting a new app:** copy `src/app-template/` — `main.mtl` assembles lib
blocks, `app.mtl` is the business logic. `task simulate:app` runs it on
http://localhost:8080.

Note: the preprocessor treats **every file as `#pragma once`**, so modules can
(and should) `#include` their own dependencies; consumers may include modules
in any order without double-definition errors.

## Two TCP Layers

There are two distinct TCP abstraction levels in this codebase. Mixing them
causes "unknown label" compile errors.

| Layer | Where | Primitives used |
|---|---|---|
| **VM native** | `lib/` | `writetcp`, `closetcp`, `tcpcb`, `listentcp` |
| **App wrapper** | `src/app/` | `tcp_write`, `tcp_close`, `tcp_set_cb`, `sock_send` |

`lib/sse_server.mtl` and `lib/http_server.mtl` use VM-native calls and are
suitable for inclusion in both lib-level tests and app code.
`src/app/srv/` modules use the app-level wrappers.

## The Sock Type

Defined in `lib/protos/sock_protos.mtl`:

```mtl
type Sock=[sockCnx sockInput sockSize sockOutput sockIndex sockCloseAfter sockCallback];;
```

| Field | Type | Purpose |
|---|---|---|
| `sockCnx` | TCP handle | Raw connection returned by `listentcp` / `tcpListen` |
| `sockInput` | string list | Accumulated inbound data chunks |
| `sockSize` | int\|nil | Total expected request size (nil = unknown) |
| `sockOutput` | string\|nil | Pending outbound data |
| `sockIndex` | int\|nil | Write cursor into `sockOutput` |
| `sockCloseAfter` | 0\|1 | Close connection once `sockOutput` is flushed |
| `sockCallback` | fun\|nil | Called with the complete request string |

## Module Usage

### String / integer / list primitives

```mtl
#include "lib/std/string.mtl"
#include "lib/std/list.mtl"
#include "lib/std/integer.mtl"
```

No dependencies. Safe to include anywhere.

### HTTP server (single request-response)

```mtl
#include "lib/net/http_server.mtl"

fun handle_request raw_request=
    "<html>hello</html>";;

fun main=
    starthttpsrv 80 #handle_request;;
```

The library calls `cbrequest` with the raw HTTP request string and sends
whatever string it returns as the HTTP/1.0 response body. The connection is
closed after the response is flushed.

### SSE server (persistent streaming)

```mtl
#include "lib/protos/sse_protos.mtl"   // if sse_server.mtl included elsewhere
#include "lib/net/sse_server.mtl"

fun on_accept cnx val msg=
    if val==TCPSTART then sse_accept_client cnx;;

fun main=
    listentcp 8080 #on_accept;;

// From any event handler:
sse_broadcast "button" "\"click\": 1";
```

**Public API:**

| Function | Signature | Description |
|---|---|---|
| `sse_accept_client` | `cnx → I` | Call from your TCP accept callback on `TCPSTART` |
| `sse_remove_client` | `cnx → I` | Remove a connection (called automatically on `TCPCLOSE`) |
| `sse_broadcast` | `type data → ?` | Send `data: {"type":"<type>", <data>}\n\n` to all clients |
| `sse_event` | `type data → S` | Build an SSE event string without sending it |

`sse_broadcast` is a no-op when there are no connected clients.

The SSE server keeps connections open and fans each event to all registered
clients. It handles partial writes via the cooperative task loop (no threads
needed). Maximum ~4–5 concurrent clients given the 1 MB RAM budget.

**Prerequisites:** the `writetcp`/`closetcp`/`tcpcb` primitives and TCP
constants must be defined before including a `lib/net` server. Include
`lib/net/tcp.mtl` (the VM-native adapter; call `netstart` in `main`) —
that's what `src/app-template` and `test/sse_test_app.mtl` do. The boot
uses its fuller `src/boot/tcpudp_emu.mtl` (adds UDP/DHCP); unit tests use
the capturing stubs in `test/lib/_test.mtl`.

### JSON builder

```mtl
#include "lib/std/json.mtl"
```

Minimal builder/parser for constructing JSON strings to pass to `sse_broadcast`.

### Network address utilities

```mtl
#include "lib/std/net.mtl"

let str_to_ip "192.168.1.1" -> ip in …
let ip_to_str ip -> s in …
let mac_to_str mac_bytes -> s in …
```

#### Wiring I/O at the app layer

```mtl
// telnet REPL: wire the socket as the write function.
// fixargN fixes the Nth argument, hence sock_send_to (string first).
let sock_create cnx nil -> sock in
let fixarg2 #sock_send_to sock -> write_fn in
let forth_interpreter_setup nil nil write_fn nil nil -> f in
    forth_interpreter_ex text f write_fn nil cb;;

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
| `sock_send_to s sock` | `sock_send` with flipped args, for `fixarg2` callback wiring |

#### http_server.mtl API

| Function | Description |
|----------|-------------|
| `starthttpsrv port cbrequest` | Listen on `port`; call `cbrequest(rawRequest)` → HTML string |
| `tcpsend sock s` | Send response body fragment |
| `tcpcloseafter sock` | Signal end of response |

## Testing

**Every lib module ships a test** mirroring its path:
`lib/<subdir>/<module>.mtl` → `test/lib/<subdir>/<module>_test.mtl`.
The whole suite runs in the simulator via:

```sh
task test
```

Failures are marked with `!!` in the output (the simulator always exits 0 —
grep for `!!`).

`test/lib/_test.mtl` provides capturing stubs for the VM TCP primitives
(`writetcp` appends to `test_tcp_out`, `test_tcp_partial` simulates short
writes) so `net/` servers are tested without hardware, and `forth_run` /
`forth_run_stack` helpers that execute a Forth program against the core
dictionary and return its buffered output / final stack rendering.

Each test file uses the framework from `test/assertions.mtl`:

```mtl
let scenario "my_module" -> s in
(
  let test "my_function" -> t in
  (
    assert_equalS "expected" actual_value;
    assert_equalI 42 computed_int
  )
);
```

Available assertions: `assert_equalI`, `assert_equalS`, `assert_nil`,
`assert_equalIL`, `assert_equalSL`, `assert_equalTL`.

For SSE integration testing without real hardware, see `test/sse_test_app.mtl` — a self-contained app that broadcasts a tick event every 2 s and can be exercised with `curl -sN http://localhost:<port>/`.
