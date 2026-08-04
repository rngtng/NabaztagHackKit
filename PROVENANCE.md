# Provenance

Sources are **vendored** (copied, not submodules - rationale in
[NABAZTAG_SDK.md](NABAZTAG_SDK.md)). This table is the backport bridge: where each tree
came from and its pinned commit, so local changes can be diffed and flowed back upstream.
Deep change histories live in the linked GitHub issues and [CHANGELOG.md](CHANGELOG.md),
not here.

| Path | Origin @ commit | Notes / local changes |
|------|-----------------|-----------------------|
| `mtl/tools/mtl_linux/` | [rngtng/mtl_linux](https://github.com/rngtng/mtl_linux) @ `7606eb3` | MTL **compiler + simulator** (Sylvain Huet's toolchain, Linux/macOS port). Fixes landed upstream; bridge already crossed. Local: parameterized `IMAGE`/`DIR` in Taskfile, `SOURCE` as a run-time arg. |
| `mtl/tools/testvm/` | `nabgcc/testvm/` (no public commit) | Native C VM smoke-runner. Local: VPATH resolves VM/net sources from `mtl/firmware/src/`, `dumpbc` extern → array fix, button Unix socket, new Dockerfile/Taskfile. |
| `mtl/tools/preprocessor/` | written for this repo | pcpp-based `#include`/`#ifdef` preprocessor, replacing piper's Perl one. |
| `mtl/tools/mockserver/` | written for this repo | stdlib-only Python mock HTTP file server (#39); serves an app's `assets/` so the sim fetches `init.forth`/`bc.jsp`/`*.mp3` end-to-end. |
| `mtl/firmware/` | `nabgcc` fork @ `2894846` (dockerized `ed3972c`) | C bytecode VM + drivers ported to `arm-none-eabi-gcc`; WPA2 branch. The `src/vm/` here is the twin of `mtl_linux`'s - fix both. |
| `mtl/boot/`, `mtl/bootV2/` | Violet/IAR boot via `firmware_nabaztag`, split into modules | Recovery path (WiFi provisioning + `bc.jsp` fetch). `boot/` is the pristine split; in `bootV2/` the device TCP/IP/DHCP/DNS/HTTP/WiFi compose the shared `mtl/lib/net` stack (#47, #103) — `ipv4/dns/http/wifi.mtl` are thin wrappers over lib. |
| `mtl/apps/piper/` | `nabaztag-piper` (ServerlessNabaztag fork) @ `3ccbf2d` | The MTL app + Forth interpreter + HTTP/telnet runtime. Most former bulk now extracted into `mtl/lib/`. |
| `mtl/lib/` | `mtl_library` curated stdlib + extracted from `nabaztag-piper` | Generic building blocks pulled out behind seams (see `mtl/lib/README.md`). Forth interpreter core: Copyright (c) 2025 Andrea Bonomi, **MIT** (per-file headers). |
| `mtl/docs/` | `mtl_library` + original `NabaztagHackKit` Ruby gem `_docs/` | MTL grammar + opcode reference. |
| `lua/firmware/lua/` | PUC-Rio **Lua 5.4.7** ([lua/lua](https://github.com/lua/lua), `lua.org/ftp`) | Vendored interpreter, whole `src/` verbatim. **4 local edits** for the 124 KB / no-FPU target: `luaconf.h` (`LUA_32BITS=1` + off-newlib number/console overrides, #106/#133), `lbaselib.c` (drop `dofile`/`loadfile`), and the parser-less image (#128) - `ldo.c` (`f_parser` text branch) + `lstate.c` (`luaX_init` call) guard the only two references to the dropped parser/lexer behind `-DLUA_NOPARSER`, so `lparser`/`llex`/`lcode` need not be linked. License **MIT**. |
| `lua/firmware/` | original to this repo; ARM startup/linker/registers copied from `mtl/firmware` @ `3a37cef` | Bare-metal Lua 5.4 port (#87). HAL drivers (led, spi, audio, i2c, rfid, motor, usb) are verbatim/trimmed ports of `mtl/firmware`'s - a register fix there may apply here, grep the sibling. `led.c` was later re-synced from `mtl/firmware` PR #45 (gamma-2.2 + background-fade engine, driven off the M11a `tick.c` tick whose ring-osc reload was corrected to 0xFC18) (#102). Per-driver detail: #89/#102/#116/#117/#118/#123/#143. `hal/config.c` (#214) and `hal/ota.c` (#235) port `mtl/firmware/src/utils/mem.c`'s OKI internal-flash writer (`write_uc_flash_sec` / `flash_uc`): `config.c` bounded to the config sector, `ota.c` the whole-image OTA writer + watchdog reset - same SPD erase/program sequences. |
| `mtl/tools/openocd/`, `lua/tools/openocd/` | original to this repo | JTAG debrick configs (Raspberry Pi bit-bang) - the host-side flashing exception (see CLAUDE.md). |

## The 802.11 stack: `mtl/firmware/src/net/` ↔ `lua/firmware/src/net/`

**These two directories are twin copies of the same vendored source.** A fix in
one almost always applies to the other - grep the sibling before concluding a
divergence is intentional, exactly as for `src/vm/` and the HAL drivers.

`aes128.c`, `eapol.c`, `hash.c` and `ieee80211.c` exist in both. `rc4.c` is mtl
only; `hash.c` is trimmed on the lua side. Two things have to be understood
before touching either copy:

**1. Both copies are CRLF.** A scripted edit that rewrites them as LF turns a
167-line fix into a 6,109-line diff and destroys this bridge. Python's
`read_text()`/`write_text()` does exactly that silently - go through
`read_bytes()`/`write_bytes()`, or edit in place. **Check `git diff --stat`
after any scripted edit**; a small change with a huge line count means the file
was rewritten. (Same rule as `CLAUDE.md` → Vendoring, repeated here because
this is where someone doing a backport is looking.)

**2. They have diverged, so a patch is a port and not a cherry-pick.** #124
scavenged WEP/WPA1/TKIP out of the lua stack, leaving it WPA2/CCMP only. mtl
still carries those branches: `eapol.c`'s RC4 group-key path and
`ieee80211.c`'s `IEEE80211_ELEMID_VENDOR` (WPA1) suite walk have no lua
counterpart at all, and every cipher decision in mtl is a two-way branch where
lua's is straight-line.

Fixes that have crossed, newest first:

| Fix | lua | mtl | Notes |
|---|---|---|---|
| Pre-auth OOB reads in the WPA handshake and the 802.11 IE walks (#292/#293/#294/#295/#296) | `4e6c9cf` | #307 | Ported, not copied. The mtl port adds two hunks with no lua counterpart, both the same defect class in the WPA1/TKIP code #124 deleted there: a bound on `eapol.c`'s RC4 GTK read, and on `ieee80211.c`'s WPA1 vendor-IE suite counts. |
| WPA2 PMK 40-byte-into-32 overflow (#228) | `818bd15` | `1a8c48c` | Fixed twin-wide; mtl's was in `vinterp.c`'s netPmk path. |

Host-native ASan coverage exists on **both** sides now and is the cheapest way
to check a port: `lua/firmware/test/host/` and `mtl/firmware/test/host/` link
the vendored sources against ~20-25 stubbed board symbols and run them under
AddressSanitizer (`task lua:firmware:test:host`, `task mtl:firmware:test:host`).
The mtl side is that track's only automated coverage of the WPA join path.

## Fixes backported upstream

Bugs found and fixed here while building the test suite, all present in upstream
`nabaztag-piper` @ `3ccbf2d` - each pinned by a test in `mtl/test/lib/`, described in
`CHANGELOG.md`: Forth `ROT`/`TUCK`/`*/`/`*/mod`, the JSON bare-literal parse loop, the
`write_fn` I/O path, and `lib/sys/time.mtl` day/month calculation.

**RT2501 USB WiFi driver** (`mtl/firmware/src/usb/`) - reference-driver backports against
Linux `rt2x00`, at algorithm/register granularity (umbrella #151): RSSI decode (#155),
bounded BBP/RF busy-bit polling (#156), BBP R3 smart-mode bit (#153), RX replay/PN
protection (#154), WPA2/CCMP GTK unwrap (#152, KAT-verified via `task mtl:firmware:test:crypto`).

**USB bring-up retry** (`mtl/firmware/src/main.c`) - backported *from* the lua track
(`lua/firmware/src/hal/wifi.c` `wifi_up()`, hardware-verified under #119) *into* mtl (#144):
the boot USB init drops VBUS and retries the controller/host re-init cycle up to 3x, so an
RT2573 that fails to enumerate - or is wedged on stale 8051 state - gets a real cold boot.
Both tracks now run the same sequence; a change to one belongs in the other.
