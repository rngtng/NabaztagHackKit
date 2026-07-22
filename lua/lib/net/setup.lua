-- net.setup - AP setup-mode provisioning portal (#233, M11e-1).
--
-- The happy-path boot flow for a rabbit with no valid creds: beacon an OPEN
-- "Nabaztag-XXXX" AP, hand a joining phone/laptop one address, serve a one-page
-- form (SSID / Wi-Fi password / app-server URL) and persist the submitted creds
-- to the nab.config flash sector (#214). #219 then reboots into the STA join
-- path; the failure UX / boot-loop guard is M11e-2 (#234), not here.
--
-- Security posture (matches V1's setup mode): the AP is OPEN and setup-mode
-- only; the creds cross the local link once, in the clear. This is deliberate -
-- setup mode is a transient, user-initiated state, not a running service - and
-- is stated on the form itself. No captive-portal DNS trickery: a fixed IP is
-- documented instead (#233 scope note).
--
-- Pure-Lua and transport-thin like its net siblings: the form build, POST parse
-- and validation are testable host-side (test/test_setup.lua); only run() touches
-- hardware (nab.wifi_ap / nab.led / net.iface).

net = net or {}
local setup = {}
net.setup = setup
local link, http = net.link, net.http

-- Fixed AP-mode addressing (documented; printed IP, no captive DNS - #233).
setup.AP_IP = "192.168.0.1"
setup.CLIENT_IP = "192.168.0.10"

-- Field caps mirror hal/config.h. The nab.config binding enforces them too, but
-- validating here turns an overlong field into a friendly re-served form rather
-- than a raised Lua error. A blank PSK means an OPEN target network; a WPA
-- passphrase is 8..63 chars.
local SSID_MAX, URL_MAX = 32, 64
local PSK_MIN, PSK_MAX = 8, 63

-- "Nabaztag-XXXX": XXXX = the last two MAC bytes as uppercase hex. mac is the
-- 6-byte binary string from nab.wifi_mac().
function setup.ap_name(mac)
  return ("Nabaztag-%02X%02X"):format(mac:byte(5), mac:byte(6))
end

-- minimal escaping so a prefilled/echoed value can't break out of the markup
local ESC = {["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;", ['"'] = "&quot;"}
local function esc(s)
  return (tostring(s):gsub('[&<>"]', ESC))
end

local HEAD = "<!DOCTYPE html><html><head><meta charset=utf-8>"
  .. "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  .. "<title>Nabaztag setup</title><style>body{font-family:sans-serif;"
  .. "margin:2em auto;max-width:22em;padding:0 1em}label{display:block;"
  .. "margin:.8em 0 .2em}input{width:100%;padding:.4em;box-sizing:border-box}"
  .. "button{margin-top:1.2em;padding:.6em 1.2em}.m{color:#b00}</style></head>"

-- The setup form. opts.msg (optional) shows an error/status line above it;
-- opts.ssids (optional array of SSID strings) becomes a datalist so the field
-- offers nearby networks. Plain HTML, no external assets - the portal is offline.
function setup.page(opts)
  opts = opts or {}
  local msg = opts.msg and ("<p class=m>" .. esc(opts.msg) .. "</p>") or ""
  local listref, datalist = "", ""
  if opts.ssids and #opts.ssids > 0 then
    local o = {}
    for _, s in ipairs(opts.ssids) do
      o[#o + 1] = '<option value="' .. esc(s) .. '">'
    end
    listref = " list=aps"
    datalist = "<datalist id=aps>" .. table.concat(o) .. "</datalist>"
  end
  return HEAD .. "<body><h1>Nabaztag setup</h1>" .. msg
    .. '<form method=POST action="/">'
    .. "<label>Wi-Fi network (SSID)</label>"
    .. '<input name=ssid' .. listref .. ' required maxlength=' .. SSID_MAX
    .. ' autofocus>' .. datalist
    .. "<label>Wi-Fi password</label>"
    .. '<input name=psk type=password maxlength=' .. PSK_MAX
    .. ' placeholder="leave blank for an open network">'
    .. "<label>App server URL</label>"
    .. '<input name=url maxlength=' .. URL_MAX .. ' placeholder="http://...">'
    .. "<button type=submit>Save &amp; connect</button></form>"
    .. "<p><small>Open setup network - your details are sent once over this"
    .. " local link, then the rabbit restarts.</small></p>"
    .. '<p><a href="/firmware">Update firmware</a></p></body></html>'
end

-- Confirmation page shown after a successful save.
function setup.saved_page(creds)
  return HEAD .. "<body><h1>Saved</h1><p>The rabbit will restart and join <b>"
    .. esc(creds.ssid) .. "</b>. You can close this page.</p></body></html>"
end

-- Validate the decoded form table f={ssid=,psk=,url=}.
-- -> creds{ssid,psk,url} | nil, message
function setup.validate(f)
  local ssid = f.ssid or ""
  local psk = f.psk or ""
  local url = f.url or ""
  if ssid == "" then return nil, "SSID is required." end
  if #ssid > SSID_MAX then
    return nil, "SSID too long (max " .. SSID_MAX .. ")."
  end
  if psk ~= "" and (#psk < PSK_MIN or #psk > PSK_MAX) then
    return nil, "Wi-Fi password must be " .. PSK_MIN .. "-" .. PSK_MAX
      .. " characters, or blank for an open network."
  end
  if #url > URL_MAX then
    return nil, "Server URL too long (max " .. URL_MAX .. ")."
  end
  return {ssid = ssid, psk = psk, url = url}
end

-- HTTP handler for iface:serve. q is the parsed request (q.method/body).
-- o.save (default nab.config) persists the creds; o.ssids seeds the datalist.
-- Returns (body, status, stop[, creds]) - serve uses the first three; run()
-- reads the 4th to learn what was saved. A POST with valid creds saves and
-- stops the portal; anything else re-serves the form.
function setup.handle(q, o)
  o = o or {}
  local save = o.save or nab.config
  if q.method == "POST" then
    local creds, err = setup.validate(http.query(q.body or ""))
    if not creds then
      return setup.page{msg = err, ssids = o.ssids}, "200 OK"
    end
    -- save -> true (written) or false (flash already held it); both mean the
    -- creds are now persisted, so provisioning is done either way.
    save(creds)
    return setup.saved_page(creds), "200 OK", true, creds
  end
  return setup.page{ssids = o.ssids}, "200 OK"
end

-- Nearby-SSID list for the datalist (nice-to-have). The scan binding is not
-- exposed to Lua yet, so this returns nil today and the datalist is simply
-- omitted; it lights up for free if a nab.wifi_scan/wifi_seen binding lands.
function setup.scan_ssids()
  if not (nab.wifi_scan and nab.wifi_seen) then return nil end
  nab.wifi_scan("")
  local seen, out = nab.wifi_seen(), {}
  for _, r in ipairs(seen or {}) do
    if r.ssid and r.ssid ~= "" then out[#out + 1] = r.ssid end
  end
  return #out > 0 and out or nil
end

-- True when the rabbit has no usable creds and should enter setup mode: a blank
-- config sector (nab.config() == nil) or a stored-but-empty SSID both count.
-- The boot orchestrator (#219) calls this to decide between run() and STA join.
function setup.needed()
  local c = nab.config()
  return not (c and c.ssid and c.ssid ~= "")
end

-- The hardware entry point: bring the radio up as an OPEN AP, hand a joining
-- client one lease, and serve the portal until creds are saved. Blocks in
-- iface:serve; returns the saved creds table. opts.iface injects a fake iface
-- (tests); opts.name overrides the derived AP SSID.
function setup.run(opts)
  opts = opts or {}
  local name = opts.name or setup.ap_name(nab.wifi_mac())
  assert(nab.wifi_ap(name))
  nab.led("nose", 0, 0, 40) -- dim blue = setup mode
  local ifc = opts.iface or net.iface.new(net.iface.nabdrv())
  ifc:dhcpd{ip = link.ip(setup.AP_IP), client_ip = link.ip(setup.CLIENT_IP)}
  ifc:dnsd() -- captive portal: resolve every hostname to us, show the page now
  local ssids = setup.scan_ssids()
  local saved, image
  ifc:serve(80, function(q)
    -- the setup page also hosts the firmware uploader (#235); route it there
    if q.path == "/firmware" and net.ota then
      local body, status, stop, img = net.ota.handle(q, {})
      if img then image = img end
      return body, status, stop
    end
    local body, status, stop, creds = setup.handle(q, {ssids = ssids})
    if creds then saved = creds end
    return body, status, stop
  end)
  if image then
    nab.led("nose", 60, 0, 60) -- magenta = flashing (solid; IRQs off shortly)
    nab.flash_firmware(image)  -- reboots into the new image; never returns
  end
  nab.led("nose", 0, 40, 0) -- green = creds captured, about to restart
  return saved
end
