# lua/lib/audio - non-blocking playback, HTTP streaming, jingles (#265)

The userland audio layer of the lua track, mirroring `mtl/lib/audio/`
(`audiolib.mtl` + `midi.mtl`): a cooperative player over the C stream HAL, an
HTTP body as a playable source, MIDI jingles, and the wheel as a volume knob.

**Nothing in this folder costs flash** — modules are compiled off-device
(`tools/luac`, `LUA_32BITS`) and shipped as `#LC` frames over the REPL into
RAM. `task lua:lib:size`: `player` 2649 B, `stream` 1860 B, `midi` 1584 B,
`volume` 670 B.

## Why

`nab.play(data)` feeds the VS1003 and **blocks until the last byte is
decoded**, so the rabbit could not make a sound and do anything else, and could
not play anything bigger than the heap. #265 split the C side into
`nab.play_start` / `nab.play_feed` / `nab.playing` / `nab.play_stop`, where
`play_feed` pushes only what the decoder can take right now and returns the
count — a short return is the flow-control signal, not an error. Everything
here is built on that one primitive.

| Module | Provides |
|--------|----------|
| `player.lua` | the engine: a queue of sources, fed a burst per `:step()` from the caller's loop; end-of-source tail flush, drain detection, stall bail-out |
| `stream.lua` | an HTTP body as a player source: head parsed off `net.http`, body queued with high-water flow control, prebuffer before the first byte is fed |
| `midi.lua` | jingles as Standard MIDI Files — the VS1003B decodes MIDI natively, so a tune is ~60 bytes per note and needs no samples |
| `volume.lua` | wheel (`nab.wheel`, ADC ch.2) → `nab.volume`, through mtl's squared taper, with a movement threshold |

Pull-style like `lib/net` and `lib/hw`: the HAL is injected, the caller owns
the clock and pumps `:step()`, so all of it unit-tests off-device
(`task lua:lib:test`, 246 assertions).

## Use

```lua
local p = audio.player.new(audio.nabdrv())

p:play(nab.tone())                       -- a byte string is a source
while p:busy() do p:step() end           -- ... and your own work goes here

p:play(audio.midi.tune{{"C5", 120}, {"E5", 120}, {"G5", 240}})
p:queue(audio.midi.note("C6", 400))      -- sources play in queue order

-- streaming (network already up - see ../net/README.md)
local ifc = net.iface.new(net.iface.nabdrv())
p:play(audio.stream.http(ifc, net.link.ip("192.168.0.10"), "srv", "/song.mp3"))
while p:busy() do p:step(); leds_and_whatever_else() end
```

`:step()` returns the state it left the player in — `idle` / `playing` /
`buffering` (the source has no bytes yet) / `draining` (tail fed, decoder
emptying) / `error` (see `p.err`) — and **never blocks**. `p:wait([ms])` is the
blocking convenience for the REPL. `apps/audio-demo.lua` is the whole thing
against animating LEDs.

### Sources

A source is a byte string, or an object with:

- `:pull()` → chunk, `""` (nothing yet — the player idles, it never feeds
  silence), or `nil` (end of stream)
- `:poll()` *(optional)* — one bounded slice of the source's own work per
  step; `stream.lua` reads its socket here
- `:close()` *(optional)* — called when the source ends or playback is stopped

### End of a source

`player.TAIL` (2048 zero endFillBytes) goes through the same feed — the VS10xx
flush the datasheet asks for, and what `audiolib.mtl` appended to its FIFO too
— then the player waits for `nab.playing()` (SCI_HDAT1) to fall, capped at
`player.DRAIN`. That is what "the sound actually finished" means; the
amplifier goes off only then.

### Flow control

On the decoder's side, as in mtl: the player feeds only what the VS1003
accepts, so a full FIFO leaves bytes in the stream's queue, and above
`stream.HIGH` bytes buffered the source stops polling the interface
altogether. Our advertised TCP window is fixed (`net/tcp.lua`), so "stop
reading" means the peer's segment goes unacknowledged and its retransmit timer
paces it down — mtl's `http_enable 0` with none of the machinery. Playback
starts only once `stream.PREBUFFER` bytes are in hand, so a slow first window
does not gap.

`net.http.response{sink = fn}` (#265) is what keeps a stream out of the heap:
body bytes are handed over as they arrive and the response holds no body, so
the file may be larger than the 1 MB of ExtRAM.

Only one TCP connection exists at a time (`net.iface` tracks a single `conn`),
so streaming and serving cannot overlap until #262 — a single stream is what
this issue scopes.

## Recording

There is no `rec.lua` here, and that is the decision, not an omission. The
microphone path (#116/#266) lives entirely in C — `nab.record(ms [, gain])` for
the blocking one-shot, `nab.rec_start`/`nab.rec_read`/`nab.rec_stop` for the
cooperative session, `nab.rec_wav(data)` to wrap drained blocks — including the
RIFF header #266 had scoped as `string.pack` up here. Moving it up would buy back
a fraction of the record path's flash — the cost and the arithmetic live in the
[budget](../../firmware/README.md#flash-budget), not here — and would cost every
capture a module load before it is playable. So it stays: `nab.record` hands Lua
a **complete playable WAV string**.

Which is the whole point for this folder: a recording is a byte string, so it is
already a first-class `player` source, with nothing to port and nothing to wrap.
Drain a session the way [`../../firmware/README.md`](../../firmware/README.md#the-nab-module)
shows, then hand the result over like any other source:

```lua
p:play(nab.rec_wav(table.concat(chunks)))
while p:busy() do p:step() end
```

`apps/walkie.lua` is a capture loop end to end (against blocking `nab.play`);
`apps/mic-test.lua` is the LED-guided hardware check. Two things the codec
imposes on any userland built here:

- **Record and playback cannot overlap.** The VS1003 is in record mode or decode
  mode, never both, so the pattern is capture-then-play — a live capture is not a
  `:pull()` source.
- **No VU level binding.** `reclib.mtl` metered the body LEDs off `recVol`; the
  lua track has no equivalent, and `mic-test.lua` reads the IMA-ADPCM block
  header's step index by hand instead. A Lua helper here would be the natural
  home if a demo ever wants one.

Getting a recording **off** the rabbit — #266's last open DoD — is an app away,
not a plumbing gap: `net.iface`'s `:serve(port, handler)` can hand a WAV to
`curl` on a laptop over GET today (it passes no content type, so the body goes
out as `text/html` — fine for `curl -o rec.wav`, not for a browser preview).
What genuinely does not exist is the *upload* direction: `lib/net` has no HTTP
client POST, so `reclib`'s app-side POST split has nothing to build on. Until
one of the two is written, every capture has been judged by ear on the device.

## Not here

- **A C-side ring buffer pumped from `event_pump`** (#265's optional bullet):
  the player already survives a slow turn by leaving bytes in the decoder's own
  2 KB FIFO, and the flash budget is the scarce thing (`lua/firmware/README.md`).
  Revisit if a real gap is measured on hardware.
- **Polyphony / a MIDI sequencer.** `midi.tune` is monophonic with rests —
  a rabbit jingle, like `midi.mtl`'s note table.
