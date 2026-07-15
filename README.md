# Nabaztag HackKit

An SDK and dockerized toolchain for the Nabaztag:tag - the WiFi rabbit. Build,
simulate, test and flash firmware for the Nabaztag:tag without installing extended toolchain:
the only host requirements are [Docker](https://www.docker.com/) and [Task](https://taskfile.dev). Everything builds in containers.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Two tracks: Lua and MTL

The repo holds two independent firmware tracks. They share the hardware, not an architecture.

| Track | Dir | What | Language stack |
|-------|-----|------|----------------|
| **mtl** | [`mtl/`](mtl/) | The classic stack, cleaned up and rebuilt: a C bytecode VM firmware running MTL apps, with a Forth interpreter on top. | C/ARM → MTL → Forth |
| **lua** | [`lua/`](lua/) | A re-architecture: bare-metal PUC-Rio Lua 5.4 on the stock board, replacing the VM. | C/ARM → Lua |

## Getting started

```
task              # list every target
task mtl:verify   # mtl track only (definition of done)
task lua:verify   # lua track only (definition of done)
```

Targets read as `<track>:<layer>:<verb>`, e.g. `task mtl:app-piper:build`,
`task mtl:boot:simulate`, `task mtl:lib:test`, `task lua:firmware:build`. Each
layer is self-contained and relocatable: `task mtl:lib:test` here ==
`cd mtl/lib && task test` there.

## Layout

```
docs/            Shared hardware notes: teardown, C-firmware architecture, JTAG
mtl/             Track A - C-VM + MTL + Forth
  firmware/        C bytecode VM, HAL, USB, audio, WPA2 (bare-metal ARM7TDMI)
  boot/  bootV2/   Boot/provisioning image (WiFi setup + OTA); boot = pristine
                   Violet split, bootV2 = the same boot converged onto lib/net
  apps/            MTL apps (piper, sse, template, ping)
  lib/             Reusable MTL standard library
  test/            MTL unit tests (lib/ coverage)
  tools/           MTL toolchain: mtl_linux (compiler+simulator), preprocessor,
                   mkfirmware, mockserver, testvm, openocd
  docs/            MTL grammar + opcode reference
lua/             Track B - bare-metal Lua
  firmware/        Lua 5.4 runtime + C HAL
  apps/            Example Lua scripts
  tools/           luac, Unicorn simulator, openocd
```

## Design rationale

Working conventions in [CLAUDE.md](CLAUDE.md); Roadmap in
[GitHub Issues](https://github.com/rngtng/NabaztagHackKit/issues).

## The mtl stack

Four layers, each depending only on those below. Flash the C VM once; iterate
everything above it without a JTAG probe.

```
Layer 3  Forth scripts (vl/*.forth)   edit-at-runtime, live REPL
           ▲ interpreted by
Layer 2  MTL app (mtl/apps/*)         + Forth interpreter, written in MTL
         + MTL stdlib (mtl/lib/*)
           ▲ compiled to bytecode by the MTL toolchain (host-side)
           ▲ remotely loaded over HTTP by
Layer 1  Boot image (mtl/boot/*)      WiFi provisioning + OTA
           ▲ packed with and runs on
Layer 0  C bytecode VM (mtl/firmware) bare-metal ARM7TDMI, flashed to Oki ML67Q4051
```

**MTL** ("Metal") is Sylvain Huet's ML/Lisp-family functional language for Violet,
compiled to bytecode for a stack-machine VM. Forth is *not* MTL - it's a small
interpreter written in MTL that runs user scripts at Layer 3. Details:
[`docs/firmware/architecture.md`](docs/firmware/architecture.md).

`mtl/lib/` is strictly layered and hardware-free (nothing references WiFi, HAL, or
app state); see [`mtl/lib/README.md`](mtl/lib/README.md).

## The lua track

An alternative Layer 0: PUC-Rio Lua 5.4 running bare-metal, with hardware behind a
thin `nab.*` HAL. JTAG-flashed, no MTL. Design principles and current state:
[`lua/firmware/README.md`](lua/firmware/README.md).

## Background

- [First-hand info from the creator](http://www.sylvain-huet.com/?lang=en#nabv2)
- [Journal du lapin](https://www.journaldulapin.com/tag/nabaztag/) - teardowns, WPA2 history
- [RedoXyde/nabgcc](https://github.com/RedoXyde/nabgcc) - GCC firmware port (mtl Layer 0 origin)
- [andreax79/ServerlessNabaztag](https://github.com/andreax79/ServerlessNabaztag) - MTL toolchain + Forth layer origin

Source origins and pinned commits: [`PROVENANCE.md`](PROVENANCE.md).
