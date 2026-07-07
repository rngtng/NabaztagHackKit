# Boot Image

The boot image is the first program that runs on the Nabaztag. It handles two responsibilities:

- **WiFi provisioning** — serves a captive-portal config page so the user can enter SSID/key
- **OTA firmware upgrade** — downloads and flashes new firmware over HTTP

It has its own minimal TCP/IP, HTTP, and DNS stack (no dependency on `lib/`) to keep the flash footprint small.

## Source layout

| File | Purpose |
|------|---------|
| `boot.mtl` | Entry point; includes all modules |
| `main.mtl` | `main` function + hardware self-test loop |
| `boot_loop.mtl` | Main WiFi provisioning loop |
| `config.mtl` | Persistent config read/write (SSID, key, IP, …) |
| `config_server.mtl` | HTTP server for the provisioning UI |
| `firmware.mtl` | OTA firmware download and flash |
| `wifi.mtl` | WiFi state machine (scan, associate, DHCP) |
| `http.mtl` | Minimal HTTP client |
| `http_server.mtl` | Minimal HTTP server |
| `dns.mtl` | DNS resolver |
| `ipv4.mtl` | IPv4/ARP/TCP/UDP stack |
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
