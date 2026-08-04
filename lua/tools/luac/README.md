# lua/tools/luac — host Lua→bytecode compiler (#128, #133)

The Lua firmware is **parser-less** (#128): it drops `lparser`/`llex`/`lcode`
(−18.9 KB) and keeps only `lundump`, so the rabbit loads `luac` bytecode but
cannot compile source at all. Every path that gets Lua onto the device therefore
compiles it here, off-device:

- **`luac`** (this image) — Lua source → stripped 32-bit device bytecode.
- **`replpipe.py`** — frames a `.lua`/`.lc` file into the `#LC` console stream
  (scripted REPL runs, and the `firmware:test` golden test).
- **`embed.py`** — bakes the resident boot chunk (`lua/boot/boot.lua`) into
  `gen/boot_lc.h` for the firmware build (there is no on-device parser to compile
  it at startup).
- **`luash.py`** — the live interactive REPL client: compiles each line you type
  and pipes it to the device console (sim or, over ssh, the Pi's UART relay).

Self-contained (own `Dockerfile` + `Taskfile.yaml`).

## The header-compatibility rule (why a distro `luac` won't do)

`lundump.c`'s `checkHeader` rejects any chunk whose header doesn't match the
target build: `LUAC_VERSION`/`FORMAT`/`DATA`, then `sizeof(Instruction)`,
`sizeof(lua_Integer)`, `sizeof(lua_Number)`, then the `LUAC_INT`/`LUAC_NUM`
sentinel byte-patterns. Our `luaconf.h` is **`LUA_32BITS`** → 4-byte
`lua_Integer`, 4-byte `float` `lua_Number`, 4-byte `Instruction`. A stock distro
`luac` (8-byte int / double) produces chunks the rabbit refuses.

So `luac` here is built **from the vendored `lua/firmware/lua/` tree with the
exact same `luaconf.h`** — the two can never drift. Container arch
(amd64/arm64) is irrelevant: dump sizes are varint-encoded and host + ARM7 are
both little-endian.

`-DLUA_HOST_LUAC` drops the device-only *Local config* block in `luaconf.h` (the
UART console + compact number helpers in the ARM firmware, M7), so the
host build falls back to the stock C library / libm. `LUA_32BITS` stays on for
both, so the dump format is identical; the device build needs no new `-D`. The
Docker image uses the **repo root** as context so its `Dockerfile` can `COPY`
the sibling `lua/` tree — the same trick `mtl/tools/testvm` uses.

## Framing protocol (`#LC`)

Raw bytecode can't ride the line-oriented UART console (chunks contain
`\n`/NUL; the device's `sh_gets` is line-based). Each chunk is framed as:

    #LC:<len>:<sum>\n      header line; len = chunk bytes (decimal),
                           sum = Fletcher-32 of the chunk (8 lowercase hex)
    <2*len hex chars>      the chunk, wrapped at 64 cols (device skips whitespace)

The device (`lua/firmware/src/main.c`, `load_lc_frame`) mallocs `len` bytes
off the external-RAM heap (with a sanity cap), hex-decodes the payload, **verifies
the checksum**, then `luaL_loadbuffer(..., "=stdin")` and runs it through
pcall+echo. A non-`#LC` line is rejected — the device has no parser.
`replpipe.py`/`luash.py` are the senders.

The checksum (#298) is the transport guard. This console is 115200 8N1 with no
hardware flow control and a 16-byte RX FIFO — the same reason the senders pace
bytes — and the loader behind it does not check what it is handed
(`lua/lundump.c`; `task lua:firmware:test:bytecode` measures that 13% of
single-byte corruptions kill the runtime, and there is no MMU on this part, so
they corrupt the heap rather than stopping). A frame that does not verify is
reported and not loaded.

It is a **checksum, not a signature**: it catches a damaged frame, not a chosen
one. Three implementations share it — `firmware/src/utils/lcframe.c`,
`replpipe.py`, `luash.py` — pinned by `lcframe_test`'s vectors and proved to
agree end to end by the simulator round-trips (`lua:firmware:test`,
`:test:inject`, `:test:sched` all pipe real frames into a real image).

## Tasks

```sh
# Compile a Lua source file to stripped 32-bit device bytecode.
task lua:apps:compile APP=foo.lua [OUT=foo.lc]

# Golden-transcript test (sim): frames run and match apps/*.expected. REGEN=1 to refresh.
task lua:firmware:test [APP=test/luac-roundtrip.lua] [REGEN=1]

# REPL - all input is compiled off-device to bytecode:
task lua:firmware:simulate:repl            # live interactive prompt (luash.py)
task lua:apps:simulate APP=foo.lua         # feed a script (or a prebuilt .lc)
task lua:firmware:flash:repl               # same, on real hardware over the Pi UART relay
```

`replpipe.py` (stdlib-only) converts a REPL script to the frame stream: a `.lua`
file becomes one frame per line, compiled with an expression-first fallback
(`return <line>`, then the line verbatim), so bare expressions echo; a `.lc` file
is shipped as a single frame. `luash.py` applies the same fallback per typed line
for the live prompt. All shell out to the `luac` image, so nothing beyond Docker
is needed on the host.
