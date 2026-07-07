# NabGCC — Architecture & Background

This document captures how the Nabaztag firmware in this repo is structured, the
languages and toolchain involved, the design decisions made here, and how it
relates to the wider Nabaztag ecosystem. It exists so the non-obvious knowledge
(especially the language stack) isn't lost.

## TL;DR

- This repo (`nabgcc`) is the **Nabaztag firmware**, ported to build with free
  GNU tools (`arm-none-eabi-gcc`) — that's what the "GCC" in the name means. It
  is **not** a compiler. The `wpa2` branch adds (experimental) WPA2 support.
- The firmware contains a **bytecode VM**. The application that runs on top of
  that VM is written in **MTL** ("Metal") — Sylvain Huet's functional bytecode
  language. (His original `.mtl` headers label it `VLISP`; the canonical name,
  and the file extension, is MTL.) **MTL is not Forth.**
- That boot app is frozen, so its compiled bytecode is **vendored** as
  `src/bc.c` and the firmware builds without any MTL tooling. The MTL compiler
  lives in the `mtl/mtl_linux` submodule and is only needed to *regenerate*
  `src/bc.c` (via `task vm-compile`) if the boot app ever changes.

## Two build pipelines

There are two independent things that end up on the device. They meet at the VM.

### Pipeline A — native firmware (this repo's main job)

```
src/*.c, sys/asm/*.s ──arm-none-eabi-gcc──> bin/Nab.elf ─> .hex / .bin
```

Bare-metal ARM7TDMI (Oki ML67Q4051) program: drivers (`src/hal`), USB host
stack (`src/usb`), WiFi (`src/net`), and the **MTL bytecode VM interpreter**
(`src/vm`). This is what gets flashed. It has **no external build dependencies**
— `make` builds it on its own.

### Pipeline B — the boot app (the program the VM runs)

```
boot.0.0.0.13.mtl  (MTL source)  ──mtl_compiler──>  bytecode  ──vendored──>  src/bc.c
```

The high-level application logic (`main.mtl` and friends) is written in MTL and
compiled to bytecode. In this repo that bytecode is committed as `src/bc.c`:

```c
const unsigned char dumpbc[36276] = { 0x61,0x6d,0x62,0x65,0x72, ... };  /* "amber"... */
```

The firmware embeds it and boots it:

- `src/main.c` → `loaderInit(<bare bytecode>)` then `interpGo();`
- `src/vm/vloader.c` parses the bytecode; `src/vm/vinterp.c` interprets it.

> ⚠️ **The vendored `dumpbc` is wrapped, not bare — this bricks boot if fed
> raw to the loader.** The committed blob (`src/bc.c` → symlink →
> `obj/dumpbc.c`, **36276 bytes**) is in the **`"amber"` HTTP-download wrapper**:
> 5 bytes `"amber"` + 8 hex ASCII length (`00008da3` = 36259) + the **bare**
> bytecode (starts `0xf5 0x47`) + trailing `"Mind"`. But `loaderInit`
> (`vloader.c:110` `loaderSizeBC`) expects **bare** bytecode at byte 0. Fed the
> wrapper, it reads `"ambe"` as a size → runaway copy loop → **boot hangs inside
> `loaderInit`, before `interpGo`/the VM ever starts** (no LEDs, no button, no
> USB servicing). This was the real cause of the long "stuck/bricked after
> flash" saga. **Fix in `src/main.c`:** skip the 13-byte `"amber"+len` header
> before `loaderInit` (the NOMINAL path already strips it in MTL,
> `boot.0.0.0.13.mtl:2567`). Bare bytecode begins at `&dumpbc + 13`. (The
> original/working `dumpbc` was bare: 36259 bytes starting `0xf5 0x47`.)
>
> **Why it was wrapped: `dumpbc` was compiled WITH SIGNING.** The MTL compiler's
> sign flag (`-s`, `SIGN=true` in the `mtl_linux` Taskfile) emits the `"amber"`
> delivery wrapper — correct for bytecode *served over HTTP* or the `.sim`
> uploaded via the config page, **wrong for the blob you embed and `loaderInit`
> directly.** **Regenerate `dumpbc` UNSIGNED** (`task vm-compile`, `SIGN=false`)
> for embedding → bare `0xf5 0x47`. Inner bytecode is identical either way
> (signing only frames it, no encryption), so the `main.c` skip-guard handles
> both and the device boots regardless — keep that guard as a re-brick safety net.

The firmware loads and runs this bytecode **unconditionally on every
power-on** — `loaderInit(&dumpbc)` then `interpGo()` run in the boot sequence
with no button or mode gate. The push button is a *runtime* input the MTL app
polls (`sysButton2`/`sysButton3` → `push_button_value()` in `src/vm/vlog.c`),
not a boot-mode selector.

#### BOOT vs NOMINAL bytecode

`dumpbc` is only the **BOOT** bytecode. The platform actually has two:

- **BOOT** — the vendored `dumpbc`, frozen in firmware. Drives the config
  interface and bring-up: WiFi auth, DHCP, firmware update.
- **NOMINAL** — the main app, *downloaded at runtime* (per-device, not in this
  repo). The BOOT app fetches it and hands control over by pushing the blob and
  executing the `OPbytecode` opcode (93 / `0x5D`), whose handler is a second
  `loaderInit` + `interpGo` at `src/vm/vinterp.c:806`. This is the other
  `loaderInit` call site besides `main.c`.

So "loads on every boot" is exact for BOOT; the NOMINAL bytecode is loaded later
over the network, through the same loader, via that opcode.

`mtl_compiler` is a **host-side** (x86, builds `-m32`) tool. The firmware build
never touches it — Pipeline B only matters when the boot app itself changes,
which for a frozen 2005 toy is essentially never.

### Where they meet

```
       MTL source ──mtl_compiler──> bytecode ──> src/bc.c ──┐
                                                            ▼
  C src ──arm-gcc──> Nab.elf [ MTL VM: vinterp/vloader ] ──runs──> bytecode
                            ▲
                compiler & interpreter must agree on opcodes (inc/vm/vbc.h)
```

Both sides must agree on the opcode set in `inc/vm/vbc.h` (152 opcodes). Change
the VM's opcodes and you must regenerate the bytecode; otherwise they're
independent.

## The language stack

Three layers, three languages — this is the part that's easy to get wrong.

| Layer | Language | Form | Lives in |
| --- | --- | --- | --- |
| VM / drivers | **C + ARM asm** | native machine code | `src/`, `sys/` |
| Boot app | **MTL (Metal)** | bytecode | `src/bc.c` (from `*.mtl`) |
| (Scripting) | — none in this repo — | — | see ecosystem below |

### MTL is functional, not Forth

MTL ("Metal") is Sylvain Huet's language for Violet. His original source headers
label it `VLISP`, but the canonical name (and the file extension) is **MTL** — so
this repo standardises on "MTL", leaving Huet's `.mtl` header comments untouched
for attribution. It is an **ML/Lisp-family functional language**, evident from
the boot source:

```
type Wifi = initW | gomasterW | masterW | gostationW _ | dhcpW _ | stationW;;
fun strstr s p i = strfind s i p 0 nil;;
var BOOT;;
const HARDWARE = 5;;
proto main 0;;
```

- `fun f x = expr;;` — prefix function application, curried args
- `type T = A | B _` — algebraic data types with constructors (ML-style)
- `;;` terminators, `nil`, `var`/`const`/`let` — Caml-light / Lisp lineage

Forth would be postfix and colon-defined (`: strstr ... ;`, `10 move-ear`).
Nothing in MTL looks like that. **MTL is not Forth.**

### The VM is a stack machine

The bytecode VM (`inc/vm/vbc.h`) is a stack machine — `OPdup`, `OPdrop`,
`OPexec`, `OPret`, `OPadd`, `OPgetlocal`, etc. A stack-based *execution model* is
why "Forth" sometimes gets mistakenly attached to this platform, but the
**source language (MTL) is functional**; only the runtime is stack-based.

## Toolchain & build

### Native

```
sudo apt install gcc-arm-none-eabi gdb-arm-none-eabi php-cli
make -j2
```

### Docker + Task (recommended — no host toolchain)

Everything is driven by `Taskfile.yaml`; the only host requirements are Docker
and [Task](https://taskfile.dev). `task firmware` builds the firmware, `task
flash-*` flashes, `task vm-*` works on the MTL app. Each target runs in a Docker
image that Task builds on demand. There are **two** images:

- **`nabgcc-build`** (`Dockerfile`) — native, multi-arch: `arm-none-eabi-gcc` +
  newlib (firmware), `gcc` (the `testvm/` harness), `php-cli` (`mkfirmware.php`),
  `doxygen`/`graphviz` (docs). Full speed on Intel and Apple Silicon.
- **`nabgcc-mtl`** (`Dockerfile.mtl`) — amd64-only: `g++` + 32-bit multilib for
  the MTL tools (`mtl_compiler`/`mtl_simu`), which build `-m32`. Runs emulated on
  Apple Silicon. Kept separate because the ARM cross-compiler is unstable under
  amd64 emulation, so the firmware build must stay in the native image.

See `README.md` for the exact target list.

## Flashing

Both methods write the **same native firmware** (`Nab.bin`); they differ only in
transport.

### 1. OpenOCD + GDB (wire / JTAG)

```
cd openocd/ && ./openocd -f ./nabaztagv2.cfg     # shell 1
make program                                      # shell 2 → gdb_load
```

A physical JTAG debug probe (FTDI Bus Blaster) wired to the board. Low-level;
can recover a bricked device. Host-only (needs the probe).

Note: the committed `openocd/openocd` is a ~9 MB Linux x86-64 binary (so it does
not run on Apple Silicon). The `*.cfg` files are the valuable part —
`target/ml67q4051.cfg` is for an obscure Oki ARM7 not in stock OpenOCD. The
binary is kept for now; it could be replaced by a distro `openocd` package (cfg
syntax would need updating for OpenOCD 0.12).

### 2. Configuration page (no probe)

```
cd bin/ && ../utils/mkfirmware.php       # Nab.bin -> firmware0.0.0.13.sim
```

`mkfirmware.php` encrypts `Nab.bin` (the `strcrypt8` routine + `inv8` table) and
wraps it in `-violet-` markers, producing a `.sim` file. You upload that through
the Nabaztag's own config mode (blue LEDs). No probe needed, but it requires a
working bootloader already on the device.

## Hardware reference

The board is an **Oki ML67Q4051** (ARM7TDMI). The facts below are captured here
because they otherwise live only on the upstream wiki (see References) — keep
them in-repo so they survive.

### Memory map

From the linker script (`sys/ml67q4051.ld`), authoritative:

| Region | Origin | Length | Notes |
| --- | --- | --- | --- |
| IntROM (flash) | `0x08000000` | 124k | physically 128k; last sector reserved for config data |
| IntRAM | `0x10000000` | 16k | on-chip; holds stack |
| ExtRAM | `0xD0000000` | 1024k | external 1 MB, the main working RAM |
| ExtROM | `0xC8000000` | 0k | unused |

### JTAG pinout

8-pin internal header (from FCC schematics), wired to a **Bus Blaster v3**
(JTAGkey buffer), driven by **OpenOCD 0.8.0** with a vendor patch for ML67Q4051
flash (the patched cfg is `openocd/target/ml67q4051.cfg`):

| Pin | Signal | Bus Blaster |
| --- | --- | --- |
| 1 | 3.3V | VTG |
| 2 | GND | GND |
| 3 | nTRST | TRST |
| 4 | TDI | TDI |
| 5 | TMS | TMS |
| 6 | TCK | TCK |
| 7 | TDO | TDO |
| 8 | RESETN | TSRST |

Flash spans `0x08000000`–`0x08020000` (128k) for `dump`/`flash write_image`.

### Serial console (debug UART)

The firmware's debug console *is* the UART: `consolestr()` →
`putst_uart()` (`inc/vm/vlog.h`), so all boot/debug output (`****Reset`, the
`loaderInit`/`vmemInit` traces, the main-loop `.` ticks) comes out here.

> ⚠️ **Not available out of the box.** The UART pads are **not populated** from
> factory — getting a serial line out requires the **`mod_serial` hardware mod**
> (soldering, https://wk.redox.ws/dev/nab/v2/mod_serial). Without that mod there
> is **no serial console**; on a stock board the only console is the JTAG
> `putst_uart` capture (which halts per string and distorts USB timing — see
> [`debug-strategy.md`](debug-strategy.md)).

Settings (apply once the mod is done), authoritative from `src/hal/uart.c`
(`init_uart`, pins PB0/PB1 in their `TX_RS232`/`RX_RS232` secondary function):

- **115200 baud, 8 data bits, no parity, 1 stop bit (8N1), no flow control.**
- **3.3 V TTL levels** — use a 3.3V USB-serial adapter (e.g. SparkFun #718) or a
  MAX232 powered at 3.3V. Do **not** wire raw RS-232 voltages to the board.

Hardware tap: a 3-pin header location on the right edge of the PCB, below the
ventral LED. Pin 1 (rightmost) is GND; pins 2–3 are TX/RX (the upstream wiki does
not pin down which is which — confirm with a scope/probe before connecting).

## Key decision: bytecode vendored, MTL toolchain isolated

The MTL compiler + simulator live in the `mtl/mtl_linux` git **submodule**. The
firmware build deliberately does **not** depend on it: the boot app is frozen, so
its compiled bytecode is **vendored** as `src/bc.c` (with a provenance header
pointing back to `boot.0.0.0.13.mtl`). `task firmware` builds straight from the
committed source tree — no submodule init, no MTL tooling required.

The submodule only matters for `task vm-compile`, which rebuilds `mtl_compiler`
(in the amd64 `nabgcc-mtl` image), recompiles `boot.0.0.0.13.mtl`, and refreshes
`src/bc.c`. Run it only if the boot app changes. `task vm-simu` similarly builds
the host simulator from the submodule.

Why this split:

- Keeps the everyday firmware build self-contained and multi-arch — the painful
  bits (relative submodule URL `../mtl_linux.git`, the `-m32` x86 toolchain) are
  quarantined behind opt-in `vm-*` targets and a separate Docker image.
- Vendoring the build-tool *output* (`src/bc.c`) instead of requiring the
  build-tool at firmware-build time is the classic embedded trade-off for a
  frozen artifact.

## Ecosystem: ServerlessNabaztag (andreax79) and the Forth question

`github.com/andreax79/ServerlessNabaztag` is a modern fork of the **same Violet
/ Huet MTL stack** (it ships `compiler/mtl_comp`, `compiler/mtl_simu` with the
same `vinterp.c`/`vloader.c`, and the same `boot/boot.0.0.0.*.mtl` files). The
confusing part: it's often described as "Forth."

It is a **three-layer cake**:

```
vl/*.forth            user scripts (Forth)          ── interpreted at runtime
        ▲ executed by
firmware/forth/*.mtl   a Forth interpreter, in MTL  ┐
firmware/*.mtl         the rest of the app, in MTL  ├─ compiled → bytecode
        ▲ runs on
C firmware (vinterp.c) the MTL stack VM             ── native ARM, flashed
```

- **C** = the MTL bytecode VM (same lineage as `nabgcc`).
- **MTL** = the whole firmware app — *including a Forth interpreter written in
  MTL* (`firmware/forth/forth.mtl`, `interpreter.mtl`, `dictionary.mtl`,
  `stack.mtl`, …).
- **Forth** = the user-facing scripting language that the MTL-Forth interpreter
  runs (`vl/config.forth`, `crontab.forth`, `weather.forth`, …).

### Why a Forth layer on top of MTL?

It buys a different **update model**, not raw capability:

| | MTL | Forth (on top) |
| --- | --- | --- |
| Runs as | compiled bytecode | interpreted text, at runtime |
| To change it | recompile on host → rebuild firmware → reflash | edit a `.forth` file the device reloads |
| Interactive | no | yes — REPL (`vl/telnet.forth`) |
| Needs toolchain | yes (host MTL compiler) | no |

The Forth-scripted bits (cron, hooks, weather) are human-timescale logic you
want to tweak without a host compiler + reflash cycle. The performance cost of
interpreting Forth-on-MTL-on-C is irrelevant there, because hot paths (mp3
decode, WiFi, the VM itself) stay in C.

### Is Forth "universal," and did he "port" it?

Forth is a real, standardized language (Chuck Moore, ~1970; ANS Forth 1994 /
Forth 2012), ubiquitous in embedded and bootloaders (Open Firmware / OpenBoot on
Sun, Apple, OLPC). But you don't "port" a Forth — Forth is a *model* (a stack, a
dictionary of words, an outer interpreter loop) and is famously tiny to
implement. andreax79 **re-implemented** a Forth dialect **in MTL**: a standard
core (`stack`, `arithmetic`, `control`, `string`, …) plus Nabaztag-specific
words (`net`, `json`, `task`, `nabaztag`). Picking a known minimal language
means free docs, familiar idioms, and a trivial implementation.

## File / directory map (this repo)

```
src/main.c          firmware entry; boots the embedded bytecode (loaderInit)
src/bc.c            symlink → obj/dumpbc.c: vendored boot bytecode (dumpbc[36276],
                    "amber"-WRAPPED — loader needs bare; see warning above)
src/vm/             the MTL bytecode VM (vinterp, vloader, vmem, vlog, vaudio, vnet)
src/hal/            drivers: audio, i2c, led, motor, rfid, spi, uart
src/usb/            USB host stack (RT2501 WiFi dongle)
src/net/            WiFi / 802.11 / crypto (incl. WPA2 work on this branch)
inc/vm/vbc.h        the 152 VM opcodes — the contract between compiler and VM
sys/                startup asm, linker script (ml67q4051.ld), low-level glue
utils/mkfirmware.php   packs Nab.bin into the encrypted config-page .sim
openocd/            JTAG flashing: bundled binary + nabaztagv2 / target cfgs
Dockerfile          multi-arch firmware build environment
```

## References

- `github.com/RedoXyde/nabgcc` — upstream of this port
- `github.com/rngtng/mtl_library` — MTL / Metal language notes
- `github.com/andreax79/ServerlessNabaztag` — modern fork; full MTL toolchain +
  a Forth-in-MTL scripting layer
- `wk.redox.ws/dev/nab/start` — upstream author's wiki hub (mostly French).
  Source for the JTAG pinout, serial-tap location and the BOOT/NOMINAL bytecode
  model above; subpages cover `v2/nabgcc`, `v2/mtl`, `v2/opcodes`, `v2/jtag`,
  `v2/mod_serial`. Content is at risk (single personal host) — the durable facts
  are mirrored into this doc.
