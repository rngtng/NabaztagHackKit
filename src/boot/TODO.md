# boot/ — stdlib extraction roadmap

## Quick wins (dead code / duplicates)

Ranked by impact. Apply in any order; each is independent.

| # | Tag | What | Where | Action |
|---|-----|------|-------|--------|
| 1 | delete | 57-line `/* */` block: `_wavcbhttp`, `wavstarthttp`, `wavtime` — commented out, every call site also commented | `boot.mtl` inside `ifdef AUDIOLIB` | Delete the `/* … */` block entirely |
| 2 | delete | `wifiConnected` — identical body to `wifiReady`, zero callers in boot | `wifi.mtl` | Delete; callers (none currently) use `wifiReady` |
| 3 | delete | `confGetbin` — same body as `confGet`, zero callers after split | `config.mtl` | Delete the function |
| 4 | delete | `/*"disabled=\"disabled\""*/` — stray commented literal inline in string | `config_server.mtl` ~line 517 | Delete the comment fragment |

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

## Path to lib/

### 1. bytecode_loader.mtl → lib/bytecode.mtl

Move as-is. The only dep is `httpgetcontent` (from `http.mtl`).
For a proper stdlib module declare the dependency as a forward proto:

```
proto httpgetcontent 1;;   // required: include http.mtl before this
```

Then any MTL app can load remote bytecode:
```
httprequest "GET" url nil #load_bytecode HTTP_NORMAL;
```

**One open question**: `_bootcbhttp` (in `boot_loop.mtl`) calls `envset envmake`
before delegating to `load_bytecode`. This is boot-specific (save wifi state so
the new bytecode restores it). In a generic lib, expose a pre-load callback:

```
fun load_bytecode_with httpreq res pre_cb=
    if pre_cb != nil then call pre_cb [];
    load_bytecode httpreq res;;
```

### 2. firmware.mtl → lib/firmware.mtl

Move as-is after one change: remove the `setleds` calls from `getfirmware` and
`firmware_flash`. LED feedback is hardware-UX, not firmware-logic. Pass an
optional progress callback instead:

```
fun getfirmware req on_decode=
    ...
    if on_decode != nil then call on_decode [0xff0000];  // red = decoding
    getbinary ...;;

fun firmware_flash req on_success=
    let getfirmware req nil -> firm in
    if firm != nil then
    (
        if on_success != nil then call on_success [0xffffff];
        flashFirmware firm 0x13fb6754 0x0407FE58
    );;
```

Callers pass a LED callback or `nil`.

### 3. http_server.mtl → lib/http_server.mtl

Already structured for lib use (private internals prefixed `_httpsrv_`).
Deps: `writetcp`, `closetcp`, `tcpcb`, `listentcp` (from ipv4.mtl / tcp stack).
Declare as protos at the top of the file and it's self-contained.

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

## mtl_merge recursive includes

`tools/preprocessor/mtl_merge` is single-level: it expands `#include` in the
root file only, not in included files. This forced the pattern of declaring all
includes in `boot.mtl` rather than in sub-modules.

Two options:
- **Fix mtl_merge**: make `process_file` recursive (trivial Ruby change, ~5 lines).
- **Use preproc.py**: `tools/preprocessor/preproc.py` already handles recursive
  includes, `#ifdef`/`#define`, and proto-dedup. Use it as the sole preprocessor:
  ```
  task preprocessor:preprocess SOURCE=src/boot/boot.mtl OUT=build/boot_merged.mtl
  ```
  and update `boot:compile` to call that instead of mtl_merge.

Switching to `preproc.py` is the better long-term choice: it also handles
`#define BOOT` (letting sub-modules use `#ifdef BOOT` guards instead of the
language-level `ifdef BOOT { }` blocks).

---

## stdlib structure proposal

```
lib/
  string.mtl        — strstr, strreplace, filterpercent, … (from util.mtl)
  list.mtl          — rev, conc, listlen, sort, select, … (from util.mtl / lib/list.mtl)
  net.mtl           — webip, webmac, useparamip  (from util.mtl)
  integer.mtl       — itoh4, itobin4, atoibin2   (from util.mtl / lib/integer.mtl)
  http_server.mtl   — starthttpsrv, tcpsend, tcpcloseafter
  firmware.mtl      — getfirmware, firmware_flash
  bytecode.mtl      — getbytecode, load_bytecode
```

The `lib/` modules should declare all external deps as `proto` at the top so
each file is self-documenting and linter-friendly.
