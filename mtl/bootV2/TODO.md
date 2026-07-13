# bootV2/ — stdlib extraction roadmap

Actionable roadmap lives in **GitHub Issues**; this file keeps the standing context.

`mtl/bootV2/` is the original monolithic `boot.mtl` split into focused modules.
The stdlib-candidate modules have **migrated** — they're now thin wrappers over `mtl/lib/`:

| File | Public API | Stdlib status |
|------|-----------|---------------|
| `bytecode_loader.mtl` | `getbytecode`, `load_bytecode` | Done — `#include "mtl/lib/sys/bytecode"` |
| `firmware.mtl` | `getfirmware`, `firmware_flash` | Done — wraps `mtl/lib/sys/firmware` with boot's LED feedback |
| `http_server.mtl` | `starthttpsrv`, `tcpsend`, `tcpcloseafter` | Done — `#include "mtl/lib/net/http_server"` |
| `ipv4.mtl` | `opentcp`, `udpsend`, `startdhcp`, `netstart`, … | Done (device) — composes `mtl/lib/net/ipv4/*` + `mtl/lib/net/dhcp.mtl`, boot names aliased. SIMU keeps `tcpudp_emu.mtl` |
| `dns.mtl` | `dnsreq`, `startdnsclient` | Done (device) — `#include "mtl/lib/net/dns.mtl"`. SIMU keeps boot's resolver |
| `http.mtl` | `httprequest`, `httpabort`, … | Done (device) — `#include "mtl/lib/net/http.mtl"` (adds 302 redirects). SIMU keeps boot's client |
| `wifi.mtl` | `wifi_init`, `wifi_run`(SIMU), `envmake`, `_wifi_*` | Done (device, #103) — `#include "mtl/lib/net/wifi.mtl"`; lib's master (AP) mode reads the button + drives the portal via `wifi_master_cb`. SIMU keeps boot's state machine |
| `config.mtl` | `confInit`, `confGet*`, `confSave` | Done (#103) — flash layout unified with `mtl/apps/piper/utils/config.mtl` (same `conf.bin`) |

The device network + WiFi stack shares `mtl/lib/net`, driven from boot's loop via
`mtl/lib/sys/task`'s `task_scheduler`; `config_seam.mtl` fills the `config_get_*` seam.

## Open items → GitHub Issues

- **#47** — boot/app convergence part 1 (done): device `ipv4.mtl`/`dns.mtl`/`http.mtl` compose `mtl/lib/net`.
- **#103** — boot/app convergence part 2 (done): `config.mtl` layout unified and `wifi.mtl` converged onto `mtl/lib/net/wifi.mtl` with button-driven master mode. Hardware-verified: the hold-button config-portal path works on the real device (AP + DHCP + HTTP config + persisted creds).
- **#125** — the station path still can't associate on hardware: `netState` goes `RT2501_S_BROKEN`. Below the MTL boundary — a C-firmware/RT2501 station-auth issue in the `mtl/firmware` image.
- **#51** — migrate `boot.mtl`'s MTL-level `ifdef BOOT { }` blocks to preprocessor `#ifdef BOOT`.
