# Boot Image

The boot image is the first program that runs on the Nabaztag. It handles two responsibilities:

- **WiFi provisioning** — serves a captive-portal config page so the user can enter SSID/key
- **OTA firmware upgrade** — downloads and flashes new firmware over HTTP

On the **device build** the TCP/IP, DHCP, DNS and HTTP layers are the shared
`lib/net` stack (the same one the apps run, device-proven in
[#46](https://github.com/rngtng/NabaztagHackKit/issues/46)); `ipv4.mtl` /
`dns.mtl` / `http.mtl` are now thin wrappers that compose `lib/net` and alias
boot's historical call names onto it (see
[#47](https://github.com/rngtng/NabaztagHackKit/issues/47)). The **simulator
build** (`SIMU`) still uses boot's own compact resolver/client over the
`tcpudp_emu.mtl` shim, so `simulate:boot` is unaffected.

Still boot-local (not yet converged, tracked in #47): `config.mtl` (the flash
layout) and `wifi.mtl` (boot keeps its own WiFi state machine + config-portal
master mode rather than lib/net/wifi's task).

## Source layout

| File | Purpose |
|------|---------|
| `boot.mtl` | Entry point; includes all modules |
| `main.mtl` | `main` function + hardware self-test loop |
| `boot_loop.mtl` | Main WiFi provisioning loop |
| `config.mtl` | Persistent config read/write (SSID, key, IP, …) |
| `config_seam.mtl` | Satisfies `lib/net`'s `config_get_*` accessors from boot's `confGet*` |
| `config_server.mtl` | HTTP server for the provisioning UI |
| `firmware.mtl` | OTA firmware download and flash |
| `wifi.mtl` | WiFi state machine (scan, associate, DHCP) |
| `http.mtl` | HTTP client — device: wraps `lib/net/http.mtl`; SIMU: boot's own |
| `http_server.mtl` | Minimal HTTP server (`lib/net/http_server.mtl`) |
| `dns.mtl` | DNS resolver — device: wraps `lib/net/dns.mtl`; SIMU: boot's own |
| `ipv4.mtl` | IPv4/ARP/TCP/UDP + DHCP — device: composes `lib/net`; boot API aliases |
| `tcpudp_emu.mtl` | Simulator shim (replaces `ipv4.mtl` when `SIMU` is defined) |
| `util.mtl` | String/list utilities |

## Build

Preprocess (merge split sources) then compile:

```
task build:boot
```

Output lands in `build/boot/merged.mtl` (merged source) and `build/boot/` (bytecode).

## Simulate

```
task simulate:boot
```

Runs with `SIMU` and `BOOT` defined. Config UI is served at `http://localhost:8080`.

## Preprocessor defines

| Define | Effect |
|--------|--------|
| `BOOT` | Include config server + boot loop + `main` entry point |
| `SIMU` | Use TCP/UDP emulation layer instead of real IPv4 stack; set `HARDWARE=5` |
| `AUDIOLIB` | Include WAV streaming support |
| `RECLIB` | Include audio recording support |

Without `BOOT`, the file compiles as a bare WiFi/network library (used for testing).

## `lib/` migration status

`src/boot/` is the original monolithic `boot.mtl` split into focused modules. Three
have already **migrated** to thin wrappers over `lib/`; the rest are still full
duplicates of their `lib/net` (and per-app `utils/config.mtl`) counterparts. Boot/app
convergence is tracked in [GitHub Issues](https://github.com/rngtng/NabaztagHackKit/issues).

| Module | `lib/` status |
|--------|---------------|
| `bytecode_loader.mtl` | Done — `#include "lib/sys/bytecode"` |
| `firmware.mtl` | Done — wraps `lib/sys/firmware` with boot's LED feedback |
| `http_server.mtl` | Done — `#include "lib/net/http_server"` |
| `config.mtl`, `ipv4.mtl`, `dns.mtl`, `http.mtl`, `wifi.mtl` | still duplicates of `lib/net` — pending on-device validation |
