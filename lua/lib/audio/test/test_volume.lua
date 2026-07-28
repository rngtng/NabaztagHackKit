-- audio.volume: the wheel curve (mtl's squared taper, on the lua HAL's
-- inverted scale) and the movement threshold that keeps it off the SCI bus.

local volume = audio.volume

-- curve ----------------------------------------------------------------------

eq(volume.curve(255), 0, "wheel fully open = attenuation 0 (loudest)")
eq(volume.curve(0), 254, "wheel closed = 254 (quietest, the nab.volume cap)")
eq(volume.curve(128), 63, "mid wheel = (127*127)>>8 = 63")
eq(volume.curve(192), 15, "the taper is squared, not linear")

local prev = -1
for w = 255, 0, -5 do
  local a = volume.curve(w)
  ok(a >= prev, "attenuation never falls as the wheel closes (w=" .. w .. ")")
  ok(a >= 0 and a <= 254, "attenuation stays in nab.volume's range")
  prev = a
end

-- tracking -------------------------------------------------------------------

local reads, set = {}, {}
local function drv(seq)
  local i = 0
  return {wheel = function()
            i = i + 1
            reads[#reads + 1] = seq[i] or seq[#seq]
            return seq[i] or seq[#seq]
          end,
          set = function(a) set[#set + 1] = a end}
end

set = {}
local v = audio.volume.new(drv{255, 255, 255})
eq(v:step(), 0, "first poll always applies")
eq(v:step(), nil, "an unmoved wheel does not rewrite SCI_VOLUME")
eq(#set, 1, "one codec write")

set = {}
v = audio.volume.new(drv{200, 202, 190})
eq(v:step(), volume.curve(200), "applies the curve of the reading")
eq(v:step(), nil, "2 counts of jitter is below the threshold")
eq(v:step(), volume.curve(190), "10 counts of movement applies")
eq(#set, 2, "two codec writes for three polls")
eq(set[2], volume.curve(190), "the second write is the new level")
eq(v.level, volume.curve(190), "level is readable")

set = {}
v = audio.volume.new(drv{0}, {invert = true})
eq(v:step(), volume.curve(255), "invert flips the wheel's ends")

set = {}
v = audio.volume.new(drv{100, 102}, {step = 1})
eq(v:step(), volume.curve(100), "first poll")
eq(v:step(), volume.curve(102), "a smaller threshold reacts to 2 counts")
