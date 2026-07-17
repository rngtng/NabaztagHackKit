# lua/net — the Lua network bootstrap (#217)

ARP, IPv4 (+ICMP echo), UDP, DHCP (client + single-lease server), TCP and
HTTP as pure-Lua modules over the raw-frame `nab.wifi_*` bindings (#216) —
the protocol layer of the lua track, per #128's architecture and V1's
precedent (V1 shipped this exact layer as MTL bytecode). There is **no C
TCP/IP** anywhere: the C side ends at the 802.11 link layer, and everything
above the LLC/SNAP header lives here.

**Nothing in this folder costs flash.** Modules are compiled off-device
(`tools/luac`, `LUA_32BITS`) and shipped as `#LC` frames over the REPL
(`task lua:firmware:repl:hw`) into RAM. Freezing a boot subset into the
resident chunk is #219's decision, fed by `task lua:net:size`.

## Layout / layering

Each file is one self-contained luac chunk that extends the global `net`
table (the device has no `require`; load order is bottom-up):

| Module | Provides |
|--------|----------|
| `link.lua` | LLC/SNAP encap/decap, RFC 1071 checksum, addr helpers. MACs/IPs are binary strings (6/4 bytes) end to end |
| `arp.lua` | request/reply build+parse, learned `arp.cache` |
| `ipv4.lua` | header build/parse (fragments dropped), ICMP echo responder |
| `udp.lua` | datagram build/parse, pseudo-header checksum |
| `dhcp.lua` | client state machine (join) + single-lease server (AP config mode, DNS pointed at the portal for #218) |
| `tcp.lua` | minimal single-connection TCP: stop-and-wait, fixed window, fixed RTO, no TIME_WAIT — sized for one HTTP exchange |
| `http.lua` | HTTP/1.0 GET builder, incremental response/request parsers, query decoding. No chunked encoding — the boot URL points at a plain file |
| `iface.lua` | glue: demux, passive MAC learning, ARP-on-demand, and the blocking flows below |

State machines are pull-style: methods return arrays of ready-to-send
packets, the caller owns the clock (`nab.time`) — that is what makes them
unit-testable off-device.

## On hardware (the DoD flows)

```lua
nab.wifi("MySSID", "psk")                    -- join (WPA2/open only, #124)
ifc = net.iface.new(net.iface.nabdrv())
lease = ifc:dhcp(15000)                      -- rabbit gets a real lease
print(net.link.ntoa(ifc.ip))
st, body = ifc:http_get(net.link.ip("192.168.0.10"), "srv", "/app.lc")
```

AP config mode (#218): `nab.wifi_ap("Nabaztag")`, then `ifc:dhcpd{ip=...,
client_ip=...}` + `ifc:serve(80, handler)`. The rabbit answers ping as soon
as `ifc.ip` is set — the first thing to try against fresh hardware.

## Tests

`task lua:net:test` (in `lua:verify`) runs `test/run.lua` under the
`tools/luac` image's host `lua` — built from the same vendored tree +
`LUA_32BITS` `luaconf.h` as the firmware, so integer width, wrap arithmetic
(TCP seq compare depends on it) and `string.pack` match the device
bit-for-bit. Fixture packets come from an independent Python generator
(`scratchpad` origin: checksums cross-checked, not self-derived); the runner
also lints every module against leaving the device stdlib
(base+table+string). Golden rule honoured: tests assert positive expected
content, never just that two runs agree.

## Size (feeds #219)

`task lua:net:size` — stripped `.lc` bytes per module. As of #217 landing:
link 1103, arp 1004, ipv4 1385, udp 788, dhcp 3420, tcp 4857, http 1881,
iface 3782 — **18,220 B total**. The boot-critical subset (join path:
link/arp/ipv4/udp/dhcp/tcp/http ≈ 14.4 KB) is what #219 must fit (compressed)
— if it doesn't, #215 (ExtRAM execution) is the lever.
