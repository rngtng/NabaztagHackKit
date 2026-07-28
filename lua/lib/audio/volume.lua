-- audio.volume - the wheel as a volume knob (#123's userland half, #265).
--
-- mtl drove `sndVol` off button3 (the wheel) through a squared curve, because
-- the codec's attenuation is linear in dB steps while a knob feels linear in
-- loudness: `255-((255-v)^2>>8)`. Same curve here, mapped onto the lua HAL's
-- inverted scale - `nab.volume` takes an *attenuation*, 0 = loudest, 254 =
-- quietest - and kept integer throughout (#213: no float near a vararg).
--
--   local v = audio.volume.new(audio.volume.nabdrv())
--   v:step()      -- from the same cooperative loop that pumps the player
--
-- `nab.wheel` (ADC ch.2) is itself unverified on hardware, and so is which end
-- of its travel is "loud" - o.invert flips it if the rabbit disagrees.

audio = audio or {}
local volume = {}
audio.volume = volume

volume.STEP = 4      -- ADC counts of movement before we touch the codec

function volume.nabdrv()
  return {wheel = nab.wheel, set = nab.volume}
end

-- wheel reading (0..255, 255 = fully open) -> nab.volume attenuation (0..254)
function volume.curve(w)
  local x = 255 - w
  local a = (x * x) >> 8
  if a > 254 then a = 254 end
  return a
end

-- drv = {wheel=fn()->0..255, set=fn(attenuation)}; o = {invert=, step=}
function volume.new(drv, o)
  o = o or {}
  local v = {drv = drv, last = nil, level = nil, step_by = o.step or volume.STEP}

  -- Poll the wheel; applies (and returns) a new attenuation only when the
  -- reading actually moved - an ADC that jitters by a count must not rewrite
  -- SCI_VOLUME on every turn of the loop.
  function v:step()
    local w = self.drv.wheel()
    if o.invert then w = 255 - w end
    if self.last and self.last - w < self.step_by
       and w - self.last < self.step_by then
      return nil
    end
    self.last = w
    self.level = volume.curve(w)
    self.drv.set(self.level)
    return self.level
  end

  return v
end
