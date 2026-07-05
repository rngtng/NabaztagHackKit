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

The remaining modules (`config.mtl`, `ipv4.mtl`, `dns.mtl`, `http.mtl`, `wifi.mtl`)
are still full duplicates of their `lib/net` (and per-app `utils/config.mtl`)
counterparts — see the tracking issue below.

## Open roadmap → GitHub Issues

- **[#47](https://github.com/rngtng/NabaztagHackKit/issues/47)** — boot/app
  convergence: port `src/boot` onto `lib/net` (deferred until
  [#46](https://github.com/rngtng/NabaztagHackKit/issues/46) proves the stack
  on-device). Includes the module-by-module unification plan for
  `config.mtl`/`wifi.mtl`/`http.mtl`/`ipv4.mtl`/`dns.mtl`.
- **[#51](https://github.com/rngtng/NabaztagHackKit/issues/51)** — migrate
  `boot.mtl`'s MTL-level `ifdef BOOT { }` blocks to preprocessor `#ifdef BOOT`
  for consistency with the rest of the conditional-include scheme.
