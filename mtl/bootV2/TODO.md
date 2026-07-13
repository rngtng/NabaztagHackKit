# boot/ — stdlib extraction roadmap

The actionable roadmap now lives in **GitHub Issues** — this file keeps only the
standing context.

## What's here

`src/boot/` is the original monolithic `boot.mtl` split into focused modules.
Three of those modules were stdlib candidates with clean interfaces and have
since **migrated** — they're now thin wrappers over `lib/`:

| File | Public API | Stdlib status |
|------|-----------|---------------|
| `bytecode_loader.mtl` | `getbytecode`, `load_bytecode` | Done — `#include "lib/sys/bytecode"` |
| `firmware.mtl` | `getfirmware`, `firmware_flash` | Done — wraps `lib/sys/firmware` with boot's LED feedback |
| `http_server.mtl` | `starthttpsrv`, `tcpsend`, `tcpcloseafter` | Done — `#include "lib/net/http_server"` |
| `ipv4.mtl` | `opentcp`, `udpsend`, `startdhcp`, `netstart`, … | Done (device) — composes `lib/net/ipv4/*` + `lib/net/dhcp.mtl`, boot names aliased. SIMU keeps `tcpudp_emu.mtl` |
| `dns.mtl` | `dnsreq`, `startdnsclient` | Done (device) — `#include "lib/net/dns.mtl"`. SIMU keeps boot's resolver |
| `http.mtl` | `httprequest`, `httpabort`, … | Done (device) — `#include "lib/net/http.mtl"` (adds 302 redirects). SIMU keeps boot's client |
| `wifi.mtl` | `wifi_init`, `wifi_run`(SIMU), `envmake`, `_wifi_*` | Done (device, #103) — `#include "lib/net/wifi.mtl"`; lib's master (AP) mode reads the button + drives the portal via `wifi_master_cb`. SIMU keeps boot's state machine |
| `config.mtl` | `confInit`, `confGet*`, `confSave` | Done (#103) — flash layout unified with `src/app-piper/utils/config.mtl` (same `conf.bin`), obsolete `CONF_LOGIN`/`CONF_PWD` dropped |

The device network + WiFi stack now shares `lib/net` (driven from boot's loop via
`lib/sys/task`'s `task_scheduler`; `config_seam.mtl` fills the `config_get_*`
seam). `config.mtl` uses app-piper's layout (no legacy-blob migration - an old
208-byte blob still brings up WiFi; boot reads none of the diverged 163-174 /
208-225 fields). Remaining gate: **on-device (JTAG) validation** of the WiFi
bring-up + hold-button config-portal path - see #103.

## Open roadmap → GitHub Issues

- **[#47](https://github.com/rngtng/NabaztagHackKit/issues/47)** — boot/app
  convergence part 1 (done): device `ipv4.mtl`/`dns.mtl`/`http.mtl` now compose
  `lib/net` (PR #101). The stack was proven on-device in
  [#46](https://github.com/rngtng/NabaztagHackKit/issues/46) first.
- **[#103](https://github.com/rngtng/NabaztagHackKit/issues/103)** — boot/app
  convergence part 2 (done): `config.mtl` layout unified (align-only, no legacy
  migration) and `wifi.mtl` converged onto `lib/net/wifi.mtl` with button-driven
  master mode. Builds + `simulate:boot` green; **hardware-verified**: JTAG-flashed
  via `task firmware:flash`, the hold-button **config-portal path works on the
  real device** (AP + DHCP + HTTP config + persisted creds). Fixed two boot-only
  bugs found on hardware: lib's auto-reconnect thrash (boot now sets
  `WIFI_NO_AUTO_RECONNECT`) and an `ears_stop` on uninitialised ears.
- **[#125](https://github.com/rngtng/NabaztagHackKit/issues/125)** — the boot
  station path still can't associate on hardware: `netState` goes
  `RT2501_S_BROKEN`. Below the MTL boundary (`netScan`/`netAuth` are called
  identically to boot's old machine) — a C-firmware/RT2501 station-auth issue in
  this never-before-flashable `src/firmware` image, split out from #103.
- **[#51](https://github.com/rngtng/NabaztagHackKit/issues/51)** — migrate
  `boot.mtl`'s MTL-level `ifdef BOOT { }` blocks to preprocessor `#ifdef BOOT`
  for consistency with the rest of the conditional-include scheme.
