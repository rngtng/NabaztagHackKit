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
| `dns.lua` | captive-portal DNS sinkhole: answer every A query with the AP IP so a joined phone's OS probe resolves to the portal and shows the config page (#233 follow-up) |
| `tcp.lua` | minimal single-connection TCP: stop-and-wait, fixed window, fixed RTO, no TIME_WAIT — sized for one HTTP exchange |
| `http.lua` | HTTP/1.0 GET builder, incremental response/request parsers, query decoding. No chunked encoding — the boot URL points at a plain file |
| `iface.lua` | glue: demux, passive MAC learning, ARP-on-demand, and the blocking flows below |
| `setup.lua` | AP setup-mode provisioning portal (#233): the one-page SSID/PSK/URL form, POST validation, and the `run()` boot flow that beacons the open AP, hands out a lease and saves creds to `nab.config` |
| `provision.lua` | the provisioning boot decision (#234): setup-vs-join, the LED vocabulary, and the persisted strike counter that guarantees a wrong-PSK / dead-AP rabbit falls back to setup instead of boot-looping |
| `ota.lua` | portal firmware upload (#235): CRC-32, whole-image header verify (magic / hardware id / length / checksum), and the `/firmware` upload page — hands only a fully verified image to `nab.flash_firmware` |

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

**Captive portal.** `run()` also starts `ifc:dnsd()` (`dns.lua`): a DNS sinkhole
that answers every A query with the AP IP. The DHCP server already advertises
this box as the client's DNS server (#217), so when the joined phone's OS runs
its connectivity probe (`captive.apple.com`, `connectivitycheck.gstatic.com`,
`msftconnecttest.com`, …) the lookup resolves to the rabbit, the probe fetches
the config page instead of its expected "success" response, and the OS pops the
"Sign in to network" sheet straight onto the portal — no typing `192.168.0.1`
into a browser. The HTTP handler already serves the form for every path, which
is what makes the hijacked probe land on it. (A printed fixed IP still works as
the fallback if a client suppresses the captive check.)

**Security posture** (matches V1's setup mode, deliberate): the AP is OPEN and
setup-mode only — the creds cross the local link once, in the clear — and the
form says so. Setup mode is a transient, user-initiated state, not a running
service. The DNS sinkhole answers only while setup mode is running. Pre-filling
the SSID field from a scan is a nice-to-have that lights up automatically once a
`nab.wifi_scan`/`wifi_seen` binding is exposed to Lua; today the datalist is
simply omitted.

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

### Firmware update over the portal (#235, `ota.lua`)

The setup page also hosts a JTAG-free firmware uploader — the recovery/dev path
that works even when the rabbit can't join any Wi-Fi. The build stamps
`bin/firmware.fw` (`tools/otaimage.py`): a 16-byte header — magic `NBZF`, header
version, target-hardware id, firmware version, image length, CRC-32 — prepended
to the raw `firmware.bin`. The uploader (a file picker at `/firmware` that POSTs the
raw bytes) feeds the blob to `ota.verify`, which checks **all** of magic /
hardware id / length / CRC **before anything touches flash**. Only a fully
verified image is handed to `nab.flash_firmware`, which erases internal flash
from address 0 and watchdog-reboots into the new image.

**Brick-safety is the whole point.** There is no A/B slot (the image is ~110 KB
of 124 KB), so verification *is* the safety: a wrong-target, truncated, or
corrupt file is refused with the current image untouched. The C writer
(`hal/ota.c`, ported from V1's proven `flash_uc`) only ever writes sectors 0..N
below the config sector, so the persisted creds survive an update. The portal
shows the verified version + checksum and a "do not unplug" warning; `setup.run`
sends that page, then flashes after the response is on the wire. Verification is
pure Lua and fully host-tested (`test_ota.lua`: happy path plus truncated /
CRC-mismatch / wrong-hardware-id / bad-magic / over-budget, each asserted to
flash nothing); the flash itself is **hardware-gated** — the on-device demo
needs the rig and a full JTAG backup first (⚠️ brick risk), like #219.

The server-driven OTA path (a downloaded image, fleet updates) is out of scope
here — it depends on #219's download path; this is the manual-upload half.

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

`task lua:lib:size` - stripped `.lc` bytes per module. As of #235: link 1103,
arp 1004, ipv4 1385, udp 788, dns 872, dhcp 3420, tcp 4857, http 1881, iface
4104, setup 4078, provision 1597, ota 3443 — **28,532 B total**. The
boot-critical subset (join path: link/arp/ipv4/udp/dhcp/tcp/http ≈ 14.4 KB) is
what #219 must fit (compressed) — if it doesn't, #215 (ExtRAM execution) is the
lever. `setup.lua` + `dns.lua` + `ota.lua` are **not** in that subset (they run
only in setup mode); `provision.lua` is small and boot-critical (it decides
between join and setup every boot), so it joins the resident subset.
