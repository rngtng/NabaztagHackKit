# Commands

A workflow-grouped tour of what you can run. Everything is a [Task](https://taskfile.dev)
target executed in Docker - the only host requirements are **Docker + Task**.

The authoritative, always-current list is the tool itself:

```sh
task              # list the main targets, with descriptions
task --list-all   # include internal/build targets
```

Targets read as **`<track>:<layer>:<verb>`** (e.g. `lua:firmware:build`,
`mtl:app-piper:simulate`). Each layer is self-contained: `task lua:lib:test`
here == `cd lua/lib && task test` there. There are two independent tracks -
**lua** (bare-metal Lua 5.4) and **mtl** (the classic C-VM + MTL + Forth); see
the [README](README.md) for the split.

Definition of done per track (run before committing):

```sh
task lua:verify   # lua track
task mtl:verify   # mtl track
task verify       # both
```

---

## Lua track

### Build

```sh
task lua:firmware:build                 # product image -> lua/firmware/bin/firmware.{elf,hex,bin}
task lua:firmware:build EXAMPLE=blink   # a standalone bring-up program (examples/<name>.c)
task lua:apps:compile APP=apps/foo.lua  # compile one Lua app to device bytecode (.lc)
task lua:boot:compile                   # compile the resident boot chunk to its C header
```

### Simulate (no hardware)

Runs the real firmware in the Unicorn simulator.

```sh
task lua:firmware:simulate                       # run the product image headless
task lua:apps:simulate APP=apps/rfid-led-ears.lua   # run a Lua app; prints its console transcript
task lua:apps:simulate APP=apps/foo.lua INJECT=t.jsonl ARGS="--leds --emit-state"
```

`INJECT=file.jsonl` drives button / RFID / ear inputs from a JSON-Lines timeline
(#42); `ARGS=--leds` shows the live 5-LED ASCII strip.

### Browser UI + in-browser REPL

The pixel-retro Nabaztag in the browser (#43): live LEDs + ears, click-inject
button/RFID, and a Lua REPL that runs on the simulated device.

```sh
task lua:simui:serve                                  # http://localhost:8080
task lua:simui:serve APP=apps/rfid-led-ears.lua PORT=9000
```

In the page: **Place Green/Yellow** (or a custom UID), **Remove tag**, **hold the
head button**, and type Lua in the **REPL box** (`print(6*7)`,
`nab.led('nose',0,0,127)`; globals persist across lines). Ctrl-C to stop.

### REPL (interactive prompt in a terminal)

```sh
task lua:firmware:simulate:repl   # live REPL against the sim (no hardware); Ctrl-D ends
task lua:firmware:flash:repl      # live REPL on real hardware over the JTAG/UART rig
task lua:firmware:flash:repl SCRIPT=apps/foo.lua   # scripted run instead of a prompt
```

Every typed line is compiled to bytecode off-device (the firmware is parser-less,
#128) and framed to the console.

### Flash real hardware (JTAG via a Raspberry Pi bridge)

```sh
task lua:firmware:flash                 # flash the product image
task lua:firmware:flash EXAMPLE=blink CAPTURE=1   # flash a bring-up prog + stream its UART
task lua:firmware:monitor               # read the live UART console read-only (no flash)
```

### Test

```sh
task lua:firmware:test:host    # host-side C unit tests (event core) under ASan/UBSan
task lua:lib:test              # host-side unit tests for the lua libs
task lua:firmware:test         # bytecode-pipeline golden (luac -> frame -> run)
task lua:firmware:test:inject  # peripheral-injection golden (button/RFID/ear)
task lua:lib:size              # device-bytecode size of every lua-lib module
```

---

## MTL track

### Build

```sh
task mtl:boot:build            # boot bytecode (run before firmware:build)
task mtl:bootV2:build          # the converged V2 boot
task mtl:firmware:build        # the C VM firmware -> build/firmware/Nab.{elf,hex,bin}
task mtl:firmware:package      # build + sign for web upload -> Nab.sim
task mtl:app-piper:build       # an MTL app (also: app-sse, app-template)
```

### Simulate

```sh
task mtl:app-piper:simulate        # app in the MTL simulator; HTTP UI at http://localhost:8080
task mtl:app-piper:simulate:mock   # end-to-end with the mock file server
task mtl:boot:simulate             # boot in config mode; config UI at http://localhost:8080
task mtl:firmware:simulate         # run the native C VM (testvm) against boot bytecode
```

### Test

```sh
task mtl:lib:test               # MTL unit tests
task mtl:firmware:test          # all firmware tests (VM smoke + memory-safety + crypto KAT)
task mtl:firmware:test:bugs     # memory-safety regression guard under AddressSanitizer
task mtl:firmware:test:crypto   # AES-128 + GTK key-unwrap known-answer test
```

### Flash real hardware

```sh
task mtl:firmware:flash         # flash the V1 boot firmware over JTAG (chains boot:build -> build)
```

---

## Common parameters

| Param | Meaning | Example |
|-------|---------|---------|
| `APP=<path>` | a Lua app source, relative to the `lua/` layer root | `APP=apps/rfid-led-ears.lua` |
| `EXAMPLE=<name>` | a C bring-up program under `lua/firmware/examples/` | `EXAMPLE=blink` |
| `PORT=<n>` | host port for a browser/HTTP UI | `PORT=9000` |
| `INJECT=<file>` | JSON-Lines peripheral-input timeline (lua sim) | `INJECT=t.jsonl` |
| `SCRIPT=<path>` | scripted input for a REPL run instead of a prompt | `SCRIPT=apps/foo.lua` |
| `EXAMPLE`/`CAPTURE`/`ARGS` | see each task's `--list` description | |

Cleanup: each layer has a `:clean` (`task lua:clean`, `task mtl:clean`, or
`task clean` for both) that removes build artifacts and Docker images.
