Nabaztag:Tag Bytecodes (MTL) compiler for Linux and Mac OSX

# Requirements

Require 32 bits architecture.
On an Amd64 architecture you will need to install some components.

On Debian:

    apt install g++-multilib libc6-dev-i386

# Compilation

Simply:

    make

# Usage

## Simulator

Simply:

    ./mtl_simu

For full options:

    ./mtl_simu -h

## Compiler

    ./mtl_compiler [-s] source output

Without `-s` the compiler writes raw bytecode, which is what the simulator
loads. With `-s` it *signs* the output for the real device's flash format:
it prepends the magic `amber` (5 bytes) followed by the bytecode size as an
8-char hex string, and appends the `Mind` footer (4 bytes).

# Docker / Task workflow

If you don't want a 32-bit toolchain on your host, run everything in a
container. This needs only [Docker](https://docs.docker.com/get-docker/) and
[Task](https://taskfile.dev/installation/) — no `g++-multilib`, no 32-bit libs.

The image is pinned to `linux/amd64` (the tools are `-m32`); on Apple Silicon
it runs under emulation, which is fine for these one-shot tools. It is built
automatically the first time you run a task, and bakes in the binaries plus the
default `config.txt` and `nominal.mtl`.

List the tasks:

    task --list

## Compile

    task compile SOURCE=app.mtl                 # → app.bin (signed, next to app.mtl)
    task compile SOURCE=app.mtl OUT=fw.bin      # override the output name
    task compile SOURCE=app.mtl SIGN=false      # raw bytecode (for the simulator)

`SOURCE` is mounted into the container and the output is written back beside it
on the host. Output is **signed** by default (`-s`, the device flash format —
see above); pass `SIGN=false` for raw bytecode such as the simulator loads.

## Simulate

    task simulate SOURCE=app.mtl                    # run app.mtl in the simulator
    task simulate SOURCE=app.mtl -- --mac 0013...   # pass extra mtl_simu options after --

The simulator runs in the foreground; stop it with a single `Ctrl+C` (the
container uses `--init` so the signal reaches the simulator). Anything after
`--` is forwarded verbatim to `mtl_simu` (see `./mtl_simu -h` for options).
