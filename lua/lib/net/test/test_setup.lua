-- net.setup: the AP setup-mode portal (#233). The form build, POST parse,
-- validation and full run() flow exercised host-side over a fake nab + fake
-- iface. Positive-content assertions throughout (repo testing rule): a form
-- with no fields, or a run that never saves, must fail here - never "A == A".

local setup, http = net.setup, net.http

-- ap_name: last two MAC bytes, uppercase hex -------------------------------

eq(setup.ap_name(H"00095b8f3ac4"), "Nabaztag-3AC4", "ap name from mac tail")
eq(setup.ap_name(H"d0021a35009f"), "Nabaztag-009F", "ap name zero-pads")

-- validate -----------------------------------------------------------------

local c = setup.validate{ssid = "MyAP", psk = "secretpw", url = "http://srv"}
eq(c and c.ssid, "MyAP", "valid creds pass ssid through")
eq(c and c.psk, "secretpw", "valid creds pass psk through")
eq(c and c.url, "http://srv", "valid creds pass url through")

local open = setup.validate{ssid = "OpenNet", psk = "", url = ""}
eq(open and open.ssid, "OpenNet", "blank psk = open network is accepted")
eq(open and open.psk, "", "open network keeps empty psk")

eq((setup.validate{ssid = "", psk = "secretpw"}), nil, "empty ssid rejected")
eq((setup.validate{ssid = "AP", psk = "short"}), nil, "psk < 8 rejected")
eq((setup.validate{ssid = "AP", psk = ("x"):rep(64)}), nil, "psk > 63 rejected")
eq((setup.validate{ssid = ("s"):rep(33), psk = ""}), nil, "ssid > 32 rejected")
eq((setup.validate{ssid = "AP", url = ("u"):rep(65)}), nil, "url > 64 rejected")
ok(select(2, setup.validate{ssid = ""}):find("SSID"), "empty-ssid msg names field")

-- page: a real, submittable form ------------------------------------------

local pg = setup.page()
ok(pg:find("<form method=POST"), "page is a POST form")
ok(pg:find('name=ssid'), "page has the ssid field")
ok(pg:find('name=psk'), "page has the psk field")
ok(pg:find('name=url'), "page has the url field")
ok(pg:find("Open setup network"), "page states the open-link security posture")
ok(not pg:find("datalist"), "no datalist without scan results")

local pg2 = setup.page{ssids = {"HomeWifi", "Cafe & Co"}}
ok(pg2:find("<datalist"), "datalist rendered from scan ssids")
ok(pg2:find('value="HomeWifi"'), "scan ssid becomes an option")
ok(pg2:find("Cafe &amp; Co"), "ssid option is html-escaped")

local pgm = setup.page{msg = "SSID is required."}
ok(pgm:find('class=m>SSID is required'), "error message shown on the form")

-- handle: GET serves the form, no save, no stop ---------------------------

local saves = {}
local function fakesave(cr) saves[#saves + 1] = cr; return true end

local body, status, stop = setup.handle({method = "GET"}, {save = fakesave})
ok(body:find("<form method=POST"), "GET returns the form")
eq(stop, nil, "GET does not stop the portal")
eq(#saves, 0, "GET saves nothing")

-- handle: POST with a urlencoded body validates, saves and stops ----------

local pbody = "ssid=MyAP&psk=secretpw&url=http%3A%2F%2Fsrv%2Fapp.lc"
local b2, s2, stop2, creds = setup.handle({method = "POST", body = pbody},
                                          {save = fakesave})
eq(stop2, true, "valid POST stops the portal")
eq(#saves, 1, "valid POST persists exactly once")
eq(saves[1].ssid, "MyAP", "saved ssid decoded from the form body")
eq(saves[1].psk, "secretpw", "saved psk decoded from the form body")
eq(saves[1].url, "http://srv/app.lc", "saved url percent-decoded")
eq(creds.ssid, "MyAP", "handle returns the saved creds as its 4th value")
ok(b2:find("Saved"), "valid POST returns the confirmation page")

-- handle: POST that fails validation re-serves the form, no save ----------

local b3, s3, stop3 = setup.handle({method = "POST", body = "ssid=&psk="},
                                   {save = fakesave})
eq(stop3, nil, "invalid POST does not stop the portal")
eq(#saves, 1, "invalid POST saves nothing")
ok(b3:find("<form method=POST"), "invalid POST re-serves the form")
ok(b3:find("class=m>"), "invalid POST shows an error message")

-- run: the whole boot flow over fakes -------------------------------------

local led, apname, dhcpd_args, persisted = {}, nil, nil, nil
nab = {
  wifi_mac = function() return H"00095b8f3ac4" end,
  wifi_ap = function(name) apname = name; return true end,
  led = function(w, r, g, b) led[#led + 1] = {w, r, g, b} end,
  config = function(cfg) persisted = cfg; return true end, -- the real save path
}
-- a fake iface whose serve delivers one valid POST to the handler
local dnsd_called = false
local fake_iface = {
  dhcpd = function(self, o) dhcpd_args = o end,
  dnsd = function(self) dnsd_called = true end,
  serve = function(self, port, handler)
    self.port = port
    handler({method = "GET"}) -- a phone loads the page first
    handler({method = "POST", body = "ssid=Home&psk=hunter2pw&url="})
  end,
}

local got = setup.run{iface = fake_iface}
eq(apname, "Nabaztag-3AC4", "run beacons the mac-derived open AP")
eq(fake_iface.port, 80, "run serves the portal on port 80")
eq(dnsd_called, true, "run starts the captive-portal DNS responder")
eq(dhcpd_args ~= nil, true, "run starts the single-lease dhcp server")
eq(net.link.ntoa(dhcpd_args.ip), setup.AP_IP, "dhcpd uses the fixed AP ip")
eq(net.link.ntoa(dhcpd_args.client_ip), setup.CLIENT_IP, "dhcpd hands out the client ip")
eq(got and got.ssid, "Home", "run returns the creds captured by the portal")
eq(got.psk, "hunter2pw", "run returns the submitted psk")
eq(persisted and persisted.ssid, "Home", "run persists the creds via nab.config")
eq(persisted.psk, "hunter2pw", "persisted psk matches the form")
eq(#led, 2, "run signals setup (blue) then captured (green)")
eq(led[1][4] > 0 and led[1][3], 0, "setup LED is blue")
eq(led[2][3] > 0 and led[2][4], 0, "captured LED is green")

nab = nil -- don't leak the fake into later test files
