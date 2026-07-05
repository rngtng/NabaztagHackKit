# testvm

Native (x86) build of the firmware's own C bytecode VM (`src/firmware/src/vm`,
`src/firmware/src/net`), with the hardware-facing modules (`hal/`, `usb/`, `utils/`)
swapped for host stubs (`stubs/`). Loads real bytecode and runs it, so it catches VM
bugs the MTL simulator (a separate, independent implementation) can't see.

Vendored from `nabgcc/testvm/` — see `PROVENANCE.md` for the origin and the local
fixes needed to get it building/running (missing headers, a stray `dbg_buffer`
redefinition, a `consolestr` macro that isn't visible outside `vm/vlog.c`, and the
Taskfile's path assumptions).

## Usage

Build the boot bytecode first, then run the smoke test against it:

```
task build:boot
task test:firmware               # bounded run (default 5s); pass = exit 0 or timeout
task test:firmware DURATION=15   # longer window
```

To watch it run interactively instead of a bounded smoke test:

```
task testvm:simulate SOURCE=build/boot/dumpbc.c
```

`SOURCE` is a `dumpbc.c` file as written by `mtl_compiler` (raw bytecode, i.e.
`SIGN=false` — the same format the simulator loads, not the signed device-flash
format). A button push can be simulated by writing one byte to the `button.sock`
Unix datagram socket under `SOCK_DIR` (default `.`, or `build/` via `test:firmware`).
