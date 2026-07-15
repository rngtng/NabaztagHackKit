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
| `lua/firmware/` | original to this repo; ARM startup/linker/registers copied from `mtl/firmware` @ `3a37cef` | Bare-metal Lua 5.4 port (#87). HAL drivers (led, spi, audio, i2c, rfid, motor, usb) are verbatim/trimmed ports of `mtl/firmware`'s - a register fix there may apply here, grep the sibling. Per-driver detail: #89/#116/#117/#118/#123/#143. |
| `mtl/tools/openocd/`, `lua/tools/openocd/` | original to this repo | JTAG debrick configs (Raspberry Pi bit-bang) - the host-side flashing exception (see CLAUDE.md). |

## Fixes backported upstream

Bugs found and fixed here while building the test suite, all present in upstream
`nabaztag-piper` @ `3ccbf2d` - each pinned by a test in `mtl/test/lib/`, described in
`CHANGELOG.md`: Forth `ROT`/`TUCK`/`*/`/`*/mod`, the JSON bare-literal parse loop, the
`write_fn` I/O path, and `lib/sys/time.mtl` day/month calculation.

**RT2501 USB WiFi driver** (`mtl/firmware/src/usb/`) - reference-driver backports against
Linux `rt2x00`, at algorithm/register granularity (umbrella #151): RSSI decode (#155),
bounded BBP/RF busy-bit polling (#156), BBP R3 smart-mode bit (#153), RX replay/PN
protection (#154), WPA2/CCMP GTK unwrap (#152, KAT-verified via `task mtl:firmware:test:crypto`).
