# app-sse — Server-Sent Events example

Streams SSE on port 80 and broadcasts a `tick` event to every connected
client every 2 seconds.

## Run it (simulator)

```bash
task mtl:app-sse:simulate
```

then, on the client side:

```bash
curl -sN http://localhost:8080/
# data: {"type":"test", "tick": 1}
# data: {"type":"test", "tick": 2}
# ...
```

## Build device bytecode

```bash
task mtl:app-sse:build
```

Like `app-template`, `main.mtl` selects its transport with the `SIMU` define:
the simulator build bridges the sim's TCP natives (`lib/net/tcp.mtl`), while the
device build pulls in the full MTL network stack (`lib/net/net.mtl` —
wifi/ARP/IP/TCP/DHCP/DNS). The real firmware VM has **no native TCP**
(`tcpListen`/`tcpSend`/… are simulator-only opcodes), so a `lib/net/tcp.mtl` +
`netstart` build silently does nothing on hardware (#37).

The device build gets its network settings from `lib/net/config_defaults.mtl`
(the shared defaults), included by the `#ifndef SIMU` block in `main.mtl`. For
your own network, `#define` the fields that differ (WiFi SSID/crypt/PMK, DHCP,
static IP fallback, proxy) before that include — see
[../../lib/README.md](../../lib/README.md).
