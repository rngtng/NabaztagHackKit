Nabaztag:Tag Bytecodes (MTL) compiler and simulator for Linux and macOS.

# Requirements

32-bit architecture. On amd64, install the multilib toolchain (Debian):

    apt install g++-multilib libc6-dev-i386

# Compilation

    make

# Usage

## Simulator

    ./mtl_simu          # full options: ./mtl_simu -h

## Compiler

    ./mtl_compiler [-s] source output

Without `-s` the compiler writes raw bytecode (what the simulator loads). With
`-s` it *signs* the output for the device's flash format: prepend magic `amber`
(5 bytes) + bytecode size as 8-char hex, append the `Mind` footer (4 bytes).

# Docker / Task workflow

Run everything in a container instead of installing a 32-bit toolchain: needs
only [Docker](https://docs.docker.com/get-docker/) and
[Task](https://taskfile.dev/installation/). The image is pinned to `linux/amd64`
(tools are `-m32`); on Apple Silicon it runs under emulation. It builds on first
use and bakes in the binaries plus the default `config.txt` and `nominal.mtl`.

Run these verbs from this dir (`mtl/tools/mtl_linux/`). Higher layers include the
tool under the `mtl:` namespace and drive it for you (`mtl:lib:test`,
`mtl:<app>:build`, `mtl:boot:simulate`); you rarely call it directly.

## Compile

    task compile SOURCE=app.mtl                 # → app.bin (signed, next to app.mtl)
    task compile SOURCE=app.mtl OUT=fw.bin      # override the output name
    task compile SOURCE=app.mtl SIGN=false      # raw bytecode (for the simulator)

`SOURCE` is mounted in and the output is written back beside it on the host.
Output is signed by default (`-s`, the device flash format); pass `SIGN=false`
for raw bytecode.

## Simulate

    task simulate SOURCE=app.mtl                                    # run app.mtl in the simulator
    task simulate SOURCE=app.mtl -- --mac 0013...                   # pass extra mtl_simu options after --
    task simulate SOURCE=app.mtl -- --serverurl host.example/path   # override CONF_SERVERURL for this run

Runs in the foreground; stop with a single `Ctrl+C` (the container uses `--init`
so the signal reaches the simulator). Anything after `--` is forwarded verbatim
to `mtl_simu`.
