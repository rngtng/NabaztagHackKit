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

`config.mtl` and `wifi.mtl` have now converged too
([#103](https://github.com/rngtng/NabaztagHackKit/issues/103)): `config.mtl`
shares `src/app-piper`'s flash layout (same `conf.bin` on the chip), and the
device build of `wifi.mtl` composes `lib/net/wifi.mtl` - the same task-driven
state machine the apps run. lib's master (AP) mode reads the button at bring-up
(`WIFI_MASTER_MODE`, defined by boot) so the hold-button config-portal recovery
path is preserved, and starts the portal via the `wifi_master_cb` seam. The
`SIMU` build keeps boot's own compact state machine over `tcpudp_emu.mtl`.

Flash to real hardware with `task firmware:flash` (JTAG, via the Pi bridge - see
`tools/openocd/README.md`). Hardware status: the config-portal (master/AP) path
is verified on-device; the **station path can't associate yet** - `netState`
reports `RT2501_S_BROKEN`, a C-firmware/RT2501 radio issue below the MTL layer
tracked in [#125](https://github.com/rngtng/NabaztagHackKit/issues/125).

## Source layout

| File | Purpose |
|------|---------|
| `boot.mtl` | Entry point; includes all modules |
| `main.mtl` | `main` function + hardware self-test loop |
| `boot_loop.mtl` | Main WiFi provisioning loop |
| `config.mtl` | Persistent config read/write (SSID, key, IP, …); flash layout shared with `src/app-piper/utils/config.mtl` |
| `config_seam.mtl` | Satisfies `lib/net`'s `config_get_*` accessors from boot's `confGet*` |
| `config_server.mtl` | HTTP server for the provisioning UI |
| `firmware.mtl` | OTA firmware download and flash |
| `wifi.mtl` | WiFi state machine (scan, associate, DHCP) — device: composes `lib/net/wifi.mtl` (button-driven master mode); SIMU: boot's own |
| `http.mtl` | HTTP client — device: wraps `lib/net/http.mtl`; SIMU: boot's own |
| `http_server.mtl` | Minimal HTTP server (`lib/net/http_server.mtl`) |
| `dns.mtl` | DNS resolver — device: wraps `lib/net/dns.mtl`; SIMU: boot's own |
| `ipv4.mtl` | IPv4/ARP/TCP/UDP + DHCP — device: composes `lib/net`; boot API aliases |
| `tcpudp_emu.mtl` | Simulator shim (replaces `ipv4.mtl` when `SIMU` is defined) |
| `util.mtl` | String/list utilities |

## Build

Preprocess (merge split sources) then compile:

```
task boot:build
```

Output lands in `build/boot/merged.mtl` (merged source) and `build/boot/` (bytecode).

## Simulate

```
task boot:simulate
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

`src/boot/` is the original monolithic `boot.mtl` split into focused modules, now
all **migrated** to thin wrappers over `lib/` on the device build (the `SIMU`
build keeps boot's compact in-file implementations). Boot/app convergence is
tracked in [GitHub Issues](https://github.com/rngtng/NabaztagHackKit/issues).

| Module | `lib/` status |
|--------|---------------|
| `bytecode_loader.mtl` | Done — `#include "lib/sys/bytecode"` |
| `firmware.mtl` | Done — wraps `lib/sys/firmware` with boot's LED feedback |
| `http_server.mtl` | Done — `#include "lib/net/http_server"` |
| `ipv4.mtl`, `dns.mtl`, `http.mtl` | Done (device, #47) — compose `lib/net`; SIMU keeps boot's own |
| `wifi.mtl` | Done (device, #103) — composes `lib/net/wifi.mtl` (button-driven master mode); SIMU keeps boot's own |
| `config.mtl` | Done (#103) — flash layout unified with `src/app-piper/utils/config.mtl` |
