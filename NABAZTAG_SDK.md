# NabaztagSDK — design rationale

Why this repo is shaped the way it is. For **what** it does and how to build it see
the [README](README.md); for **where each source came from** see
[PROVENANCE.md](PROVENANCE.md); for **working conventions** see [CLAUDE.md](CLAUDE.md);
the **roadmap lives in [GitHub Issues](https://github.com/rngtng/NabaztagHackKit/issues)**.

## Goal & scope

One SDK covering **low firmware (C/ARM) → MTL development → Forth scripting**, with a
stable **toolchain (build / simulate / test / flash-upload)** as the priority.
High-level apps (Home Assistant, weather, TTS) are **out of scope** — they consume the
SDK, they aren't part of it.

## Synthesized from four repos

The SDK takes the best piece from each of four prior projects (origins + commits in
[PROVENANCE.md](PROVENANCE.md)):

| Repo | Contributes | → SDK role |
|------|-------------|-----------|
| **firmware_nabaztag** | the original Violet/IAR archive; drivers + WPA2 origin | source of truth for `src/firmware` |
| **nabgcc** | the C bytecode VM + drivers ported to `arm-none-eabi-gcc`, with Docker/Task | **Layer 0** — the flashed firmware |
| **mtl_library** | the MTL language itself: curated stdlib, precompiler, test framework, grammar/opcode docs | `lib/` + `test/` + `docs/metal/` |
| **nabaztag-piper** (ServerlessNabaztag fork) | a full MTL app + a **Forth interpreter written in MTL** + HTTP/telnet runtime | the app, the Forth layer, OTA/REPL |

`nabgcc` and `nabaztag-piper` are **not competitors** — they solve different layers of
the same stack. **Tie-break when unsure: prefer `nabaztag-piper`.**

## The three-tier update model (the spine)

The reason to combine these repos: together they give a graceful update gradient from
"needs a JTAG probe" to "edit a text file, the device reloads it." This *is* the
requested low-firmware → high-level-dev spectrum.

| Tier | Artifact | Transport | How often it changes |
|------|----------|-----------|----------------------|
| **T0** | C VM firmware (`Nab.bin`) | JTAG (OpenOCD+GDB) **or** config-page `.sim` | almost never (frozen VM) |
| **T0.5** | boot bytecode (`src/boot/`) | embedded in the flash image | rarely (recovery path) |
| **T1** | MTL app bytecode (`bc.jsp`) | HTTP pull from your web server at boot | per MTL feature |
| **T2** | Forth scripts (`*.forth`) | HTTP `load-srv` / telnet REPL, live | per tweak, interactively |

The "serverless" trick: flash a minimal VM **once** (T0), then iterate the whole app by
redeploying `bc.jsp` to a local web server (T1), and hot-tweak behaviour over telnet
without any recompile (T2). The SDK's job is to make all three tiers ergonomic,
simulated, and tested. (Boot-embedded vs remote-load embedding strategies:
[`docs/firmware/architecture.md`](docs/firmware/architecture.md).)

## Three test/simulate levels — complementary, keep all

- **`task simulate:firmware`** (`tools/testvm/`) — compiles the **C VM** natively and
  smoke-runs bytecode → catches VM bugs.
- **`task simulate:boot` / `simulate:app`** (`mtl_simu`) — runs an **MTL app** on the
  host with faked hardware → catches app-logic bugs.
- **`task test`** (`test/`) — the **MTL assertion framework** run through the simulator
  → unit tests for `lib/`.

## Language & tooling choices

- **Python for all glue; C/C++ only as Docker-built binaries.** No Perl/Ruby/PHP.
- **Docker + Task** so the host needs only those two. The genuinely painful deps —
  `arm-none-eabi-gcc` and the **32-bit** (`-m32`) MTL toolchain — are containerized,
  making builds reproducible and the host requirement portable. Two accepted caveats:
  the MTL image is **amd64-only** (emulated, slow, on Apple Silicon — fine for one-shot
  compiles), and **JTAG needs real USB**, so flashing runs host-side, outside Docker.

## Compiler warning policy (firmware)

Both firmware Makefiles build with `-Wall -Wextra -Wpedantic -Wpointer-arith
-Wcast-align`. The target is a **clean build — zero warnings** — because the `-Os`
WPA-join regression (fixed with `-fno-strict-aliasing`) showed that real bugs hide in
the warnings we stop reading (#150). `-Wcast-align` matters specially on ARM7TDMI: an
unaligned 32-bit load *rotates silently* instead of faulting, so a punned
`uint8_t* → uint32_t*` frame view can corrupt data with no crash.

Each warning is triaged, not blanket-suppressed:
- **net/crypto** casts are fixed to be genuinely aligned or endian-explicit
  (`hash.c` MD5 block is now `uint32_t[16]`; `vnet.c` `netChk` reads its checksum
  byte-wise so it is correct at any offset).
- **verified-aligned** buffers (VM audio blocks, the `aligned(4)` rt2501 frame/firmware,
  the `container_of` list macro) keep the cast but route it through `(void *)` with a
  one-line rationale, which tells `-Wcast-align` the alignment is intentional.
- genuine logic issues are fixed outright (an unguarded `switch` fall-through in
  `ieee80211.c` mis-parsed unknown WEP crypts as WPA ciphers).

Keep new firmware code warning-clean; `-Werror` on a curated subset is a future CI gate.

## Vendoring, not submodules

Sources are **copied in**, not submoduled, with origin repo + commit + local changes
recorded in [PROVENANCE.md](PROVENANCE.md) — the backport bridge. This keeps the
everyday build self-contained and multi-arch and quarantines the painful bits (relative
submodule URLs, the `-m32` toolchain) behind opt-in targets. Never vendor secrets;
exclude build artifacts and rebuild in Docker. (Rules in [CLAUDE.md](CLAUDE.md).)
