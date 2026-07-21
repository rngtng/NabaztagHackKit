# lua/lib/net - the Lua network bootstrap (#217)

ARP, IPv4 (+ICMP echo), UDP, DHCP (client + single-lease server), TCP and
HTTP as pure-Lua modules over the raw-frame `nab.wifi_*` bindings (#216) —
the protocol layer of the lua track, per #128's architecture and V1's
precedent (V1 shipped this exact layer as MTL bytecode). There is **no C
TCP/IP** anywhere: the C side ends at the 802.11 link layer, and everything
above the LLC/SNAP header lives here.

**Nothing in this folder costs flash.** Modules are compiled off-device
(`tools/luac`, `LUA_32BITS`) and shipped as `#LC` frames over the REPL
(`task lua:firmware:flash:repl`) into RAM. Freezing a boot subset into the
resident chunk is #219's decision, fed by `task lua:lib:size`.

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
| `setup.lua` | AP setup-mode provisioning portal (#233): the one-page SSID/PSK/URL form, POST validation, and the `run()` boot flow that beacons the open AP, hands out a lease and saves creds to `nab.config` |
| `provision.lua` | the provisioning boot decision (#234): setup-vs-join, the LED vocabulary, and the persisted strike counter that guarantees a wrong-PSK / dead-AP rabbit falls back to setup instead of boot-looping |

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

### Setup mode / provisioning (#233, M11e-1)

`setup.lua` stitches AP mode + DHCP server + HTTP form + `nab.config` into the
factory-blank provisioning happy path:

```lua
if net.setup.needed() then net.setup.run() end   -- no creds -> portal
```

`run()` derives an SSID `Nabaztag-XXXX` (last two MAC bytes), beacons it as an
**open** network (nose LED dim blue), serves one form at `http://192.168.0.1/`
(SSID / Wi-Fi password / app-server URL) to a joined phone, validates the POST,
writes the creds to the flash config sector and returns them (LED green). The
reboot into the STA join path is #219; the wrong-PSK / boot-loop failure UX is
M11e-2 (#234) — this module is the happy path only. The form build, POST parse
and validation are decomposed (`setup.page` / `setup.handle` / `setup.validate`)
so they test host-side without hardware.

**Security posture** (matches V1's setup mode, deliberate): the AP is OPEN and
setup-mode only — the creds cross the local link once, in the clear — and the
form says so. Setup mode is a transient, user-initiated state, not a running
service. No captive-portal DNS trickery: the fixed `192.168.0.1` is documented
rather than spoofed (#233 scope). Pre-filling the SSID field from a scan is a
nice-to-have that lights up automatically once a `nab.wifi_scan`/`wifi_seen`
binding is exposed to Lua; today the datalist is simply omitted.

### Provisioning failure UX / boot fallback (#234, M11e-2)

`provision.lua` is the robustness half — it decides, on every boot, between a
join attempt and setup mode, and guarantees the rabbit can never wedge in a
boot loop:

```lua
local r = net.provision.run()   -- "joined" | "retry" | "setup-*"
```

`provision.boot(hooks)` is the pure decision (the `run()` wrapper wires the real
`nab.*` + `net.setup`):

1. **Head-button held at boot** → setup mode (the recovery hatch), before
   anything else.
2. **No / empty creds** → setup mode.
3. **`fails` counter already at `MAX_FAILS`** (default 3) → setup mode, no join.
4. Otherwise **join** (`nab.wifi`): success clears the strike counter (a no-op
   write when it was already 0) and returns `"joined"`; failure bumps the
   counter and returns `"retry"` — the caller reboots and `boot()` runs again,
   entering setup once the counter reaches `MAX_FAILS`. Bounded by construction,
   so `"retry"` can never loop forever.

The strike counter is `nab.config`'s `fails` field (#214/#234): a plain
read-modify-write, **power-loss tolerant** — a torn write leaves the sector
blank/invalid, which the next boot reads as "no creds" and enters setup, never a
crash (worst case one lost increment). A fresh provisioning through `net.setup`
writes creds with no `fails` key, so it resets the strikes to 0.

`nab.wifi` returns a third value on failure — a stable `reason` tag (`"radio"`,
`"notfound"`, `"auth"`, `"timeout"`) classified in the wifi HAL — which drives
the LED vocabulary (nose LED: blue setup / amber connecting / red auth-fail /
green connected). `"auth"` is a best-effort wrong-PSK hint (an encrypted AP that
never completed the join), not proof.

**Hardware DoD is pending a JTAG session** (this environment has no rabbit): the
software is complete and `lua:verify` green, but the end-to-end rows — wrong-PSK
falls back to setup within a bounded time, button-hold forces setup, the
persisted counter drives the transition — need the rig, like #219's on-device
proof.

## Tests

`task lua:lib:test` (in `lua:verify`) runs `test/run.lua` under the
`tools/luac` image's host `lua` — built from the same vendored tree +
`LUA_32BITS` `luaconf.h` as the firmware, so integer width, wrap arithmetic
(TCP seq compare depends on it) and `string.pack` match the device
bit-for-bit. Fixture packets come from an independent Python generator
(`scratchpad` origin: checksums cross-checked, not self-derived); the runner
also lints every module against leaving the device stdlib
(base+table+string). Golden rule honoured: tests assert positive expected
content, never just that two runs agree.

## Size (feeds #219)

`task lua:lib:size` - stripped `.lc` bytes per module. As of #234:
link 1103, arp 1004, ipv4 1385, udp 788, dhcp 3420, tcp 4857, http 1881,
iface 3782, setup 3823, provision 1597 — **23,640 B total**. The boot-critical
subset (join path: link/arp/ipv4/udp/dhcp/tcp/http ≈ 14.4 KB) is what #219 must
fit (compressed) — if it doesn't, #215 (ExtRAM execution) is the lever.
`setup.lua` is **not** in that subset (it runs only with no creds); `provision.lua`
is small and boot-critical (it decides between join and setup every boot), so it
joins the resident subset.
