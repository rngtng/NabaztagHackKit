-- net.ota - portal firmware upload + brick-safe verification (#235).
--
-- The JTAG-free "manually upload a new firmware" path: the setup page (#233)
-- accepts a .sim file (tools/otaimage.py output = a 16-byte header + the raw
-- image), and this module verifies it ENTIRELY before anything touches flash -
-- magic, target-hardware id, exact length, CRC-32 - then hands only a fully
-- verified image to nab.flash_firmware, which erases internal flash from
-- address 0 and reboots into it. Any check failing aborts with the current
-- image untouched: there is no A/B slot, so verification IS the brick-safety.
--
-- verify/parse/CRC are pure Lua and test host-side (test/test_ota.lua) against a
-- fake nab.flash_firmware; only apply()'s final call reaches hardware.

net = net or {}
local ota = {}
net.ota = ota

ota.MAGIC = "NBZF"       -- must match tools/otaimage.py
ota.HW_ID = 1            -- ml67q4051 lua board (matches otaimage.py / builds)
ota.HDR_LEN = 16
ota.MAX_IMAGE = 0x1F000  -- below the config sector (matches hal/ota.h)

-- Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/final ~0), so the device
-- recomputes exactly what tools/otaimage.py (zlib.crc32) stamped.
--
-- Byte table, built once on first use. The bit-at-a-time form this replaced ran
-- eight shift/xor/and/negate iterations per byte: measured at 6,725,000 Lua VM
-- instructions for a real 117,516-byte image against 1,297,000 for the identical
-- result here (#304). The table is 256 integers in the heap and this lib ships
-- as bytecode over the REPL, so it costs no flash at all.
local CRC_T

local function crc_table()
  if CRC_T then return CRC_T end
  local t = {}
  for i = 0, 255 do
    local c = i
    for _ = 1, 8 do
      c = (c >> 1) ~ (0xEDB88320 & -(c & 1))
    end
    t[i] = c
  end
  CRC_T = t
  return t
end

-- Bytes hashed between two calls to `pump`. 4 KB measured at ~29 hand-backs
-- over a full image for no measurable extra work (1,298,000 vs 1,297,000).
ota.CHUNK = 4096

-- crc32(s [, pump]) -> u32. `pump` (optional) is called after every CHUNK bytes.
--
-- The chunking is not about speed, it is about not being a hole. ota.verify runs
-- inside iface:serve's handler, between a phone's firmware POST and its
-- response, and while it ran nothing polled the interface, fed the decoder,
-- stepped an ear or drained an event - with the phone's TCP retransmitting into
-- a stack that was not listening. That is the hole #283 closed for
-- nab.wait/nab.play/nab.wifi_recv, and it was still open here (#304).
function ota.crc32(s, pump)
  local t = crc_table()
  local byte = string.byte
  local crc = 0xFFFFFFFF
  local n, i = #s, 1

  while i <= n do
    local stop = i + ota.CHUNK - 1
    if stop > n then stop = n end
    for j = i, stop do
      crc = (crc >> 8) ~ t[(crc ~ byte(s, j)) & 0xFF]
    end
    i = stop + 1
    if pump then pump() end
  end
  return (~crc) & 0xFFFFFFFF
end

-- Parse + verify blob (= header .. image). opts.hw_id overrides the expected
-- target; opts.pump (optional) is handed back the caller's reactor every
-- ota.CHUNK bytes of hashing, so a portal can keep serving while it verifies
-- (#304). -> image, meta{version,len,crc} | nil, errmsg. Touches no hardware.
function ota.verify(blob, opts)
  opts = opts or {}
  if #blob < ota.HDR_LEN then
    return nil, "file too small to be a firmware image"
  end
  if blob:sub(1, 4) ~= ota.MAGIC then
    return nil, "not a Nabaztag firmware image (bad magic)"
  end
  local _, hver, hw, fwver, ilen, icrc = string.unpack(">c4BBI2I4I4", blob)
  if hver ~= 1 then
    return nil, "unsupported header version " .. hver
  end
  local want = opts.hw_id or ota.HW_ID
  if hw ~= want then
    return nil, ("wrong hardware id (image %d, this rabbit %d)"):format(hw, want)
  end
  if ilen > ota.MAX_IMAGE then
    return nil, "image exceeds the flash budget"
  end
  local image = blob:sub(ota.HDR_LEN + 1)
  if #image ~= ilen then
    return nil, ("length mismatch (header %d, got %d)"):format(ilen, #image)
  end
  local got = ota.crc32(image, opts.pump)
  if got ~= icrc then
    return nil, ("checksum mismatch (header %08x, got %08x)"):format(icrc, got)
  end
  return image, {version = fwver, len = ilen, crc = icrc}
end

-- Verify then flash. hooks.flash (default nab.flash_firmware) receives the raw
-- image; on real hardware it reboots and never returns. -> meta on a (faked)
-- success, or nil, errmsg on any pre-flash failure. NOTHING is flashed unless
-- verify passed.
function ota.apply(blob, opts, hooks)
  hooks = hooks or {}
  local image, meta = ota.verify(blob, opts)
  if not image then return nil, meta end
  local flash = hooks.flash or nab.flash_firmware
  flash(image)   -- reboots into the new image on hardware
  return meta    -- only reached under a fake flash (tests)
end

local ESC = {["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;", ['"'] = "&quot;"}
local function esc(s) return (tostring(s):gsub('[&<>"]', ESC)) end

local HEAD = "<!DOCTYPE html><html><head><meta charset=utf-8>"
  .. "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  .. "<title>Nabaztag firmware</title><style>body{font-family:sans-serif;"
  .. "margin:2em auto;max-width:22em;padding:0 1em}button{margin:1em 0;"
  .. "padding:.6em 1.2em}.m{color:#b00}code{word-break:break-all}</style></head>"

-- The upload page. opts.msg shows a status/error line. Inline JS reads the
-- picked file and POSTs its raw bytes to /firmware (no multipart), then swaps
-- in the server's response.
function ota.page(opts)
  opts = opts or {}
  local msg = opts.msg and ("<p class=m>" .. esc(opts.msg) .. "</p>") or ""
  return HEAD .. "<body><h1>Update firmware</h1>" .. msg
    .. "<p>Choose a Nabaztag firmware file (<code>.sim</code>) built by the SDK."
    .. " The rabbit verifies magic, hardware id, length and checksum before it"
    .. " flashes.</p>"
    .. "<input type=file id=f accept=.sim>"
    .. "<button onclick=up()>Upload &amp; flash</button>"
    .. "<p><small><b>Do not unplug during flashing.</b> There is no second"
    .. " firmware slot - a bad file is refused before flashing, but a power cut"
    .. " mid-flash needs JTAG recovery.</small></p>"
    .. '<p><a href="/">&larr; back to Wi-Fi setup</a></p>'
    .. "<script>function up(){var f=document.getElementById('f').files[0];"
    .. "if(!f){return;}var r=new FileReader();r.onload=function(){"
    .. "fetch('/firmware',{method:'POST',body:new Uint8Array(r.result)})"
    .. ".then(function(x){return x.text();}).then(function(t){"
    .. "document.open();document.write(t);document.close();});};"
    .. "r.readAsArrayBuffer(f);}</script></body></html>"
end

-- Shown after a verified upload, while the flash runs.
function ota.flashing_page(meta)
  return HEAD .. "<body><h1>Flashing firmware</h1>"
    .. ("<p>Verified image v%d, checksum <code>%08x</code>.</p>")
       :format(meta.version, meta.crc)
    .. "<p><b>Do not unplug the rabbit.</b> It will reboot into the new"
    .. " firmware in a few seconds.</p></body></html>"
end

-- Portal route for /firmware (called from setup.run's serve loop). o.hw_id is
-- passed to verify. GET -> upload page. POST -> verify the raw body; ok ->
-- (flashing page, "200 OK", true, image); bad -> (upload page + error). serve()
-- uses the first three returns; setup.run reads the 4th (the verified image)
-- and flashes it AFTER the response has been sent.
function ota.handle(q, o)
  o = o or {}
  if q.method == "POST" then
    local image, meta = ota.verify(q.body or "", o)
    if not image then
      return ota.page{msg = "Upload failed: " .. meta}, "200 OK"
    end
    return ota.flashing_page(meta), "200 OK", true, image
  end
  return ota.page{}
end
