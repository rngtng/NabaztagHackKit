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

The device network stack now shares `lib/net` (driven from boot's loop via
`lib/sys/task`'s `task_scheduler`; `config_seam.mtl` fills the `config_get_*`
seam). Two modules remain boot-local:

- **`config.mtl`** — still its own flash layout (`CONF_LOGIN`/`CONF_PWD` at
  163/169 where app-piper has `CONF_LANGUAGE`/`CONF_CITY`/…). Unifying the
  layout + migration is deferred (needs a magic-versioning decision to tell the
  old 208-byte blob from the new one — both use magic `0x47`).
- **`wifi.mtl`** — boot keeps its own WiFi state machine + config-portal master
  mode. `lib/net/wifi.mtl` runs as a scheduler task and its master-mode entry is
  still `// TODO` (doesn't read the button), so a straight swap would regress the
  hold-button-to-enter-config-portal recovery path.

See the tracking issue below.

## Open roadmap → GitHub Issues

- **[#47](https://github.com/rngtng/NabaztagHackKit/issues/47)** — boot/app
  convergence: port `src/boot` onto `lib/net` (deferred until
  [#46](https://github.com/rngtng/NabaztagHackKit/issues/46) proves the stack
  on-device). Includes the module-by-module unification plan for
  `config.mtl`/`wifi.mtl`/`http.mtl`/`ipv4.mtl`/`dns.mtl`.
- **[#51](https://github.com/rngtng/NabaztagHackKit/issues/51)** — migrate
  `boot.mtl`'s MTL-level `ifdef BOOT { }` blocks to preprocessor `#ifdef BOOT`
  for consistency with the rest of the conditional-include scheme.
