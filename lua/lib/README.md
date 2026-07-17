# lua/lib - Lua standard libraries (lua track)

Pure-Lua libraries for the lua-track firmware, one folder per lib. They mirror
`mtl/lib/`: behaviour lives in Lua over the thin `nab.*` HAL (per the firmware
design principles), compiled off-device to bytecode and loaded over the REPL -
**nothing here costs flash** until #219 freezes a boot subset into the resident
chunk.

## Libs

- **`net/`** (#217) - the network bootstrap: ARP, IPv4 (+ICMP echo), UDP, DHCP
  (client + single-lease server), TCP and HTTP as pure-Lua modules over the
  raw-frame `nab.wifi_*` bindings (#216). See `net/README.md`.

## Tasks

- `task lua:lib:test` - host-side unit tests for every lib (in `lua:verify`).
- `task lua:lib:size` - stripped device-bytecode size per module, grouped by
  lib (feeds #219's flash budget).

Both **auto-discover** libs: a new lib is a new subfolder with its own modules
and a `test/run.lua`; it is picked up with no edits to `Taskfile.yaml`. Tests
run under the `tools/luac` image's host `lua`, built from the same vendored Lua
tree + `LUA_32BITS` `luaconf.h` as the device, so integer width and
`string.pack` semantics match the rabbit exactly.
