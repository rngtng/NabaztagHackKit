-- #265 acceptance demo: audio that does not stop the rabbit.
--
-- Plays a jingle, then (if a server address is given) streams an MP3 off the
-- network - while the belly ring animates and the head button still answers,
-- all from the one cooperative loop. That is the whole point of the
-- non-blocking feed: with nab.play() the LEDs would freeze until the sound
-- ended.
--
-- Load order over the REPL (nothing here is resident; #219 decides what is):
--   task lua:firmware:flash:repl SCRIPT=lib/audio/player.lua
--   ... midi.lua, volume.lua, and for streaming: lib/net/*.lua + audio/stream.lua
--   task lua:firmware:flash:repl SCRIPT=apps/audio-demo.lua
--
-- Streaming needs the network up first (see lib/net/README.md):
--   nab.wifi("MySSID", "psk"); IFC = net.iface.new(net.iface.nabdrv())
--   IFC:dhcp(15000); SRV = net.link.ip("192.168.0.10"); PATH = "/song.mp3"
-- Without IFC set it plays the jingle and stops.

local RING = {'belly', 'right', 'bottom', 'left'}
local JINGLE = {{"C5", 120}, {"E5", 120}, {"G5", 120}, {"C6", 300},
                {"r", 100}, {"G5", 120}, {"C6", 400}}

local p = audio.player.new(audio.nabdrv())
local vol = nab.wheel and audio.volume.new(audio.volume.nabdrv()) or nil

-- One turn of everything: audio, LEDs, volume knob, button. Called as fast as
-- the loop goes round - the decoder takes what it can, the rest just runs.
local led, t0 = 1, nab.time()
local function tick()
  p:step()
  if vol then vol:step() end
  if (nab.time() - t0) & 0xFFFFFFFF > 150 then     -- ~7 fps light chase
    t0 = nab.time()
    nab.led(RING[led], 0, 0, 0)
    led = led % #RING + 1
    nab.led(RING[led], 0, 90, 120)
  end
  return not nab.button()                          -- button ends the demo
end

local function play(src, what)
  print("playing " .. what)
  p:play(src)
  while p:busy() do
    if not tick() then p:stop(); print("stopped"); return false end
  end
  if p.err then print("audio error: " .. p.err) end
  return true
end

print("=== audio demo (#265): LEDs keep moving while it plays ===")
nab.volume(0x20)
play(audio.midi.tune(JINGLE), "a MIDI jingle")

if IFC and SRV then
  play(audio.stream.http(IFC, SRV, HOST or "srv", PATH or "/song.mp3"),
       "an HTTP stream")
else
  print("set IFC/SRV/PATH (see the header) to stream from the network")
end

for i = 1, #RING do nab.led(RING[i], 0, 0, 0) end
print("=== audio demo done ===")
