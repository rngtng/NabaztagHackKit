# Boot Image

The first program that runs on the Nabaztag. Two responsibilities:

- **WiFi provisioning** — captive-portal config page for SSID/key
- **OTA firmware upgrade** — downloads and flashes new firmware over HTTP

On the **device build** the TCP/IP, DHCP, DNS and HTTP layers are the shared
`mtl/lib/net` stack (the same one the apps run); `ipv4.mtl` / `dns.mtl` /
`http.mtl` are thin wrappers that compose `mtl/lib/net` and alias boot's
historical call names onto it (#47). `config.mtl` shares `mtl/apps/piper`'s
flash layout (same `conf.bin`), and the device `wifi.mtl` composes
`mtl/lib/net/wifi.mtl` — the same task-driven state machine the apps run,
with lib's master (AP) mode reading the button at bring-up (`WIFI_MASTER_MODE`)
to preserve the hold-button config-portal recovery path (#103). The **simulator
build** (`SIMU`) keeps boot's own compact resolver/client/state machine over the
`tcpudp_emu.mtl` shim, so `mtl:bootV2:simulate` is unaffected.

Flash to real hardware with `task mtl:firmware:flash` (JTAG, via the Pi bridge —
see `../tools/openocd/README.md`). Hardware status: the config-portal (master/AP)
path is verified on-device; the **station path can't associate yet** — `netState`
reports `RT2501_S_BROKEN`, a C-firmware/RT2501 radio issue below the MTL layer
(#125).

## Source layout

| File | Purpose |
|------|---------|
| `boot.mtl` | Entry point; includes all modules |
| `main.mtl` | `main` function + hardware self-test loop |
| `boot_loop.mtl` | Main WiFi provisioning loop |
| `config.mtl` | Persistent config read/write (SSID, key, IP, …); flash layout shared with `mtl/apps/piper/utils/config.mtl` |
| `config_seam.mtl` | Satisfies `mtl/lib/net`'s `config_get_*` accessors from boot's `confGet*` |
| `config_server.mtl` | HTTP server for the provisioning UI |
| `firmware.mtl` | OTA firmware download and flash |
| `wifi.mtl` | WiFi state machine (scan, associate, DHCP) — device: composes `mtl/lib/net/wifi.mtl`; SIMU: boot's own |
| `http.mtl` | HTTP client — device: wraps `mtl/lib/net/http.mtl`; SIMU: boot's own |
| `http_server.mtl` | Minimal HTTP server (`mtl/lib/net/http_server.mtl`) |
| `dns.mtl` | DNS resolver — device: wraps `mtl/lib/net/dns.mtl`; SIMU: boot's own |
| `ipv4.mtl` | IPv4/ARP/TCP/UDP + DHCP — device: composes `mtl/lib/net`; boot API aliases |
| `tcpudp_emu.mtl` | Simulator shim (replaces `ipv4.mtl` when `SIMU` is defined) |
| `util.mtl` | String/list utilities |

## Build

Preprocess (merge split sources) then compile:

```
task mtl:bootV2:build
```

Output lands in `build/bootV2/merged.mtl` (merged source) and `build/bootV2/` (bytecode).

## Simulate

```
task mtl:bootV2:simulate
```

Runs with `SIMU` and `BOOT` defined. Config UI at `http://localhost:8080`.

## Preprocessor defines

| Define | Effect |
|--------|--------|
| `BOOT` | Include config server + boot loop + `main` entry point |
| `SIMU` | Use TCP/UDP emulation instead of the real IPv4 stack; set `HARDWARE=5` |
| `AUDIOLIB` | Include WAV streaming support |
| `RECLIB` | Include audio recording support |

Without `BOOT`, the file compiles as a bare WiFi/network library (used for testing).
