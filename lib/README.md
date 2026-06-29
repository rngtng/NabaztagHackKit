# lib — Reusable MTL Library

Portable MTL modules for the Nabaztag firmware. Each file is self-contained
and `#include`-able on demand. No module pulls in more than it needs.

The `lib/` modules should declare all external deps as `proto` at the top so
each file is self-documenting and linter-friendly.


## Architecture Proposal

```
lib/
├── protos/          Forward declarations (include before the implementing file)
│   ├── sock_protos.mtl   Sock type definition — shared by http_server & sse_server
│   ├── sse_protos.mtl    SSE public API protos
│   ├── forth_protos.mtl  Forth interpreter protos
│   ├── ascii_protos.mtl
│   └── word_protos.mtl
│
├── forth/           Forth interpreter (sub-modules, assembled by forth.mtl)
│   ├── interpreter.mtl
│   ├── stack.mtl / arithmetic.mtl / comparison.mtl / logical.mtl
│   ├── control.mtl / list.mtl / string.mtl / output.mtl
│   └── …
│
├── std  # Primitives
│   ├── string.mtl        String helpers (strcat, strsub, itoa, …)
│   ├── integer.mtl       Integer utilities
│   ├── list.mtl          Linked-list helpers (hd, tl, rev, …)
│   ├── buffer.mtl        Byte-buffer utilities
│   ├── b64.mtl           Base64 encode/decode
│   ├── url.mtl           URL encode/decode
│   ├── json.mtl          Minimal JSON builder/parser
│   ├── md5.mtl           MD5 hash
│   └── net.mtl           IP/MAC address conversions
│
├── net # Networking
│   ├── sock.mtl          Sock write/close helpers (app-level, uses tcp_write/tcp_close)
│   ├── http_server.mtl   Single-request HTTP/1.0 server (closes after response)
│   └── sse_server.mtl    Persistent SSE server (keeps connections open)
│
└── High-level
    ├── forth.mtl         Forth interpreter entry point
    ├── firmware.mtl      Firmware-level helpers
    ├── bytecode.mtl      Bytecode utilities
    ├── system.mtl        System/OS helpers
    └── echo.mtl          Debug output helpers
```

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

**Prerequisites:** The including file must define (or stub) these VM primitives
before `#include "lib/net/sse_server.mtl"`:

```mtl
fun writetcp cnx msg offset= …;;
fun closetcp cnx= …;;
fun tcpcb cnx cb= …;;
const TCPWRITE=0;;
const TCPCLOSE=-1;;
```

In the firmware these come from `src/boot/tcpudp_emu.mtl`. In unit tests,
stub them out — see `test/lib/_test.mtl` for the pattern.

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

## Testing

Unit tests live in `test/lib/<module>_test.mtl` and are run via:

```sh
task test:lib
```

Each test file uses the framework from `test/lib/_test.mtl`:

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

For SSE integration testing without real hardware, see `test/sse_test_app.mtl`
— a self-contained app that broadcasts a tick event every 2 s and can be
exercised with `curl -sN http://localhost:<port>/`.
