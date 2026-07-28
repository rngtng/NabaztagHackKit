# lua/lib - Lua standard libraries (lua track)

Pure-Lua libraries for the lua-track firmware, one folder per lib. They mirror
`mtl/lib/`: behaviour lives in Lua over the thin `nab.*` HAL (per the firmware
design principles), compiled off-device to bytecode and loaded over the REPL -
**nothing here costs flash** until #219 freezes a boot subset into the resident
chunk.

## Libs

- **`net/`** (#217) - the network bootstrap: ARP, IPv4 (+ICMP echo), UDP, DHCP
  (client + single-lease server),  DNS (resolver + captive-portal sinkhole), 
  TCP and HTTP as pure-Lua modules over the raw-frame `nab.wifi_*` bindings (#216). See `net/README.md`.
- **`hw/`** (#263) - the hardware behaviour layer, mirroring `mtl/lib/hw/`:
  `ears.lua` turns the raw encoder edge count into homing and absolute ear
  positions (the state machine `hal/motor.h` points at). See `hw/README.md`.
- **`audio/`** (#265) - non-blocking playback, mirroring `mtl/lib/audio/`:
  `player.lua` keeps the VS1003 fed from the cooperative loop (so sound and
  LEDs/net/REPL coexist), `stream.lua` plays an HTTP body bigger than the heap,
  `midi.lua` builds jingles the codec decodes natively, `volume.lua` maps the
  wheel to `nab.volume`. See `audio/README.md`.


## Tasks

- `task lua:lib:test` - host-side unit tests for every lib (in `lua:verify`).
- `task lua:lib:size` - stripped device-bytecode size per module, grouped by
  lib (feeds #219's flash budget).

Both **auto-discover** libs: a new lib is a new subfolder with its own modules
and a `test/run.lua`; it is picked up with no edits to `Taskfile.yaml`. Tests
run under the `tools/luac` image's host `lua`, built from the same vendored Lua
tree + `LUA_32BITS` `luaconf.h` as the device, so integer width and
`string.pack` semantics match the rabbit exactly.
