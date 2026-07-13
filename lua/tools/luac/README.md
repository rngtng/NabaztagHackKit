# lua/tools/luac — host Lua→bytecode compiler (#133)

The Lua firmware end-game (M11, #119) is a **parser-less** wifi image: it drops
`lparser`/`llex`/`lcode` (−19 KB) and keeps only `lundump`, so the rabbit loads
`luac` bytecode but can no longer compile source. Every path that feeds Lua
*source* to the device needs a host-side compile step — this tool, plus the
tethered REPL pipe (`replpipe.py`).

Self-contained (own `Dockerfile` + `Taskfile.yaml`). Exercised against the
current parser-ful `APP=lua` dev image, which accepts bytecode *and* source —
which is what makes the pipe verifiable (round-trip test).

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
semihosting console + compact number helpers in the ARM firmware, M7), so the
host build falls back to the stock C library / libm. `LUA_32BITS` stays on for
both, so the dump format is identical; the device build needs no new `-D`. The
Docker image uses the **repo root** as context so its `Dockerfile` can `COPY`
the sibling `lua/` tree — the same trick `mtl/tools/testvm` uses.

## Framing protocol (`#LC`)

Raw bytecode can't ride the line-oriented semihosting console (chunks contain
`\n`/NUL; the device's `sh_gets` is line-based). Each chunk is framed as:

    #LC:<len>\n            header line; len = chunk size in bytes (decimal)
    <2*len hex chars>      the chunk, wrapped at 64 cols (device skips whitespace)

The device (`lua/firmware/src/app/lua.c`, `load_lc_frame`) mallocs `len` bytes
off the external-RAM heap (with a sanity cap), hex-decodes the payload, then
`luaL_loadbuffer(..., "=stdin")` and runs it through the same pcall+echo path as
a source line. `replpipe.py` is the sender.

## Tasks

```sh
# Compile a Lua source file to stripped 32-bit device bytecode.
task lua:firmware:luac SOURCE=foo.lua OUT=foo.lc

# Round-trip test (sim): SCRIPT fed as source vs. as #LC frames must match.
task lua:firmware:test:luac [SCRIPT=apps/luac-roundtrip.lua]

# Drive the sim / hardware REPL with bytecode instead of source:
task lua:firmware:repl SCRIPT=foo.lua LC=1        # (or a .lc SCRIPT)
task lua:firmware:repl:hw SCRIPT=foo.lua LC=1
```

`replpipe.py` (stdlib-only) converts REPL input to the frame stream: a `.lua`
file becomes one frame per line, compiled with the same expression-first
fallback the device uses (`return <line>`, then the line verbatim), so bare
expressions still echo and the prompt count matches feeding the source. A `.lc`
file is shipped as a single frame. It shells out to the `luac` image, so a line
can be compiled off-device with no host toolchain beyond Docker.
