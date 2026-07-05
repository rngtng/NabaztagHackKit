# boot/ — stdlib extraction roadmap

## What's here

`src/boot/` is the original monolithic `boot.mtl` split into focused modules.
Three of those modules are already stdlib candidates with clean interfaces:

| File | Public API | Stdlib status |
|------|-----------|---------------|
| `bytecode_loader.mtl` | `getbytecode`, `load_bytecode` | Ready — zero boot-specific deps |
| `firmware.mtl` | `getfirmware`, `firmware_flash` | Near-ready — one LED dep |
| `http_server.mtl` | `starthttpsrv`, `tcpsend`, `tcpcloseafter` | Near-ready — depends on TCP stack |

The remaining modules (`config.mtl`, `ipv4.mtl`, `dns.mtl`, `http.mtl`, `wifi.mtl`,
…) are either boot-specific or duplicates of code that already lives in `src/app/`.

---

## Unifying boot/ and app/ (larger effort)

`src/boot/` and `src/app/` have diverged from a common ancestor. The duplicate
areas and the delta needed to unify them:

### config.mtl vs src/app/utils/config.mtl

Boot config has CONF_LOGIN (163) and CONF_PWD (169) where app has CONF_LANGUAGE,
CONF_DST, CONF_CITY, CONF_TAICHI_FREQ. These layouts **cannot coexist** on the
same flash chip — pick one layout for the target firmware.

Steps to unify:
1. Decide which fields survive (LOGIN/PWD are obsolete in app; language/city are
   new).
2. Write a migration: on boot, if CONF_MAGIC==0x47 and CONF_LENGTH==208, convert
   old layout to new.
3. Replace boot's `config.mtl` with an `#include` of app's `config.mtl` and add
   the migration call in `confInit`.

### wifi.mtl vs src/app/net/wifi.mtl

Boot wifi uses `confGetDhcp`, `setleds`, direct timer calls. App wifi uses
`config_get_dhcp`, `leds_set_all`, the task scheduler. Functionally identical.

Steps to unify:
1. Alias boot's config API to app's: `fun config_get_dhcp = confGetDhcp;;`
   (or vice versa — pick one canonical name and update all call sites).
2. Replace `setleds` with `leds_set_all` (or alias).
3. Add a thin task-scheduler shim for boot (a simple loopcb wrapper that calls
   the wifi task function).
4. Replace boot's `wifi.mtl` with an `#include` of app's.

### http.mtl vs src/app/net/http.mtl

App's `Httpreq` has extra fields (`aliveH`, `sonH`, `http_req_redir_depth`) for
redirect handling. Boot's is simpler.

Steps to unify:
1. Add redirect support to boot's HTTP client (adopt app version), OR
2. Factor out the redirect layer so the simple client is a subset.

### ipv4.mtl / dns.mtl vs src/app versions

These are essentially identical — the app versions are direct extractions from
boot. The block: app's `tcp.mtl` includes `protos/task_protos.mtl` which defines
the `Task` type. Boot has no task scheduler.

Quick fix: move the `Task` type definition (from `task_protos.mtl`) into a
standalone `lib/task.mtl` that can be included optionally. Then boot can include
`app/ipv4/tcp.mtl` directly (with `task.mtl` providing the type).

---

## consistent conditionals

`task build:boot` and `task simulate:boot` both call `preprocessor:preprocess`
before compiling. Defines (`BOOT`, `SIMU`) are passed via `DEFINES=` so the
preprocessor expands conditional blocks correctly.

One open item: boot.mtl still uses MTL-level `ifdef BOOT { }` blocks for some
conditionals instead of preprocessor `#ifdef BOOT`. These work but are processed
by the MTL compiler rather than the preprocessor, so `#include` directives inside
them are always expanded regardless of the flag. Migrating to `#ifdef BOOT` /
`#ifndef SIMU` preprocessor directives would make the conditionals consistent.
