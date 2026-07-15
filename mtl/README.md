# NabaztagSDK — MTL - design rationale

This documents the **mtl** track (C-VM + MTL + Forth). The **lua** track is a separate
re-architecture with its own rationale in [`lua/firmware/README.md`](lua/firmware/README.md).

## Goal & scope

One SDK covering **low firmware (C/ARM) → MTL development → Forth scripting**, with a
stable **toolchain (build / simulate / test / flash)** as the priority. High-level apps
(Home Assistant, weather, TTS) are **out of scope** - they consume the SDK, they aren't
part of it.

## Synthesized from four repos

The mtl track takes the best piece from each of four prior projects (commits in
[PROVENANCE.md](PROVENANCE.md)):

| Repo | Contributes | → SDK role |
|------|-------------|-----------|
| **firmware_nabaztag** | original Violet/IAR archive; drivers + WPA2 origin | source of truth for `mtl/firmware` |
| **nabgcc** | the C bytecode VM + drivers ported to `arm-none-eabi-gcc`, with Docker/Task | **Layer 0** - the flashed firmware |
| **mtl_library** | the MTL language: curated stdlib, precompiler, test framework, grammar/opcode docs | `mtl/lib` + `mtl/test` + `mtl/docs` |
| **nabaztag-piper** (ServerlessNabaztag fork) | a full MTL app + a **Forth interpreter written in MTL** + HTTP/telnet runtime | the app, the Forth layer, OTA/REPL |

`nabgcc` and `nabaztag-piper` solve different layers of the same stack, not competitors.
**Tie-break when unsure: prefer `nabaztag-piper`.**

## The three-tier update model (the spine)

Combining these repos gives a graceful update gradient from "needs a JTAG probe" to
"edit a text file, the device reloads it."

| Tier | Artifact | Transport | Changes |
|------|----------|-----------|---------|
| **T0** | C VM firmware (`Nab.bin`) | JTAG, or config-page `.sim` | almost never (frozen VM) |
| **T0.5** | boot bytecode (`mtl/boot`) | embedded in the flash image | rarely (recovery path) |
| **T1** | MTL app bytecode (`bc.jsp`) | HTTP pull from your server at boot | per MTL feature |
| **T2** | Forth scripts (`*.forth`) | HTTP / telnet REPL, live | per tweak, interactively |

Flash a minimal VM **once** (T0), iterate the whole app by redeploying `bc.jsp` to a
local web server (T1), hot-tweak behaviour over telnet with no recompile (T2). The SDK
makes all three tiers ergonomic, simulated, and tested. (Boot-embedded vs remote-load
strategies: [`docs/firmware/architecture.md`](docs/firmware/architecture.md).)

## Three test/simulate levels — keep all

- **`task mtl:firmware:test`** (`mtl/tools/testvm`) - compiles the **C VM** natively and
  smoke-runs bytecode → catches VM bugs.
- **`task mtl:boot:simulate` / `mtl:<app>:simulate`** (`mtl_simu`) - runs an **MTL app** on
  the host with faked hardware → catches app-logic bugs.
- **`task mtl:lib:test`** (`mtl/test`) - the **MTL assertion framework** run through the
  simulator → unit tests for `mtl/lib`.

## Language & tooling choices

- **Python for all glue; C/C++ only as Docker-built binaries.** No Perl/Ruby/PHP.
- **Docker + Task** so the host needs only those two. The painful deps -
  `arm-none-eabi-gcc` and the **32-bit** MTL toolchain - are containerized for
  reproducible builds. Two caveats: the MTL image is **amd64-only** (emulated, slow on
  Apple Silicon) and **JTAG needs real USB**, so flashing runs host-side, outside Docker.

## Compiler warning policy (firmware)

Both firmware Makefiles build with `-Wall -Wextra -Wpedantic -Wpointer-arith
-Wcast-align`, targeting a **clean, zero-warning build** - the `-Os` WPA-join regression
showed real bugs hide in warnings we stop reading. `-Wcast-align` matters on ARM7TDMI: an
unaligned 32-bit load *rotates silently* instead of faulting, so a punned
`uint8_t* → uint32_t*` view can corrupt data with no crash. Each warning is triaged, not
blanket-suppressed: net/crypto casts fixed to be genuinely aligned or endian-explicit;
verified-aligned buffers keep the cast via `(void *)` with a one-line rationale; genuine
logic issues fixed outright. Keep new firmware code warning-clean.

## Vendoring, not submodules

Sources are **copied in**, not submoduled, with origin repo + commit + local changes in
[PROVENANCE.md](PROVENANCE.md) - the backport bridge. This keeps the build self-contained
and multi-arch and quarantines the painful bits (submodule URLs, the `-m32` toolchain)
behind opt-in targets. Never vendor secrets; exclude build artifacts and rebuild in
Docker. (Rules in [CLAUDE.md](CLAUDE.md).)
