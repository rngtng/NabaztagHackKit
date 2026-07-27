-- net.provision: the provisioning boot decision + strike-counter fallback
-- (#234). Pure logic over fake hooks - every branch of boot() and the LED
-- vocabulary, asserted on concrete outcomes (repo testing rule).

local provision = net.provision

-- A hook harness: records led states / saves / setup entries, and drives join
-- from a scripted result. cfg is the "flash" (mutated in place by save).
local function harness(o)
  o = o or {}
  local h = {
    leds = {}, saves = {}, joins = 0, setups = 0, flash = o.cfg,
    max_fails = o.max_fails,
  }
  h.button_held = function() return o.button == true end
  h.read_cfg = function() return h.flash end
  h.save = function(cfg)
    h.saves[#h.saves + 1] = {ssid = cfg.ssid, fails = cfg.fails}
    h.flash = cfg -- persisted
    return true
  end
  h.join = function(cfg)
    h.joins = h.joins + 1
    h.joined_with = cfg
    if o.join_ok then return true end
    return false, o.reason or "timeout"
  end
  h.led = function(state) h.leds[#h.leds + 1] = state end
  h.setup = function() h.setups = h.setups + 1 end
  return h
end

local CREDS = function(fails)
  return {ssid = "Home", psk = "hunter2pw", url = "http://s", fails = fails}
end

-- button-hold forces setup, before anything else ---------------------------

local h = harness{button = true, cfg = CREDS(0), join_ok = true}
local r = provision.boot(h)
eq(r, "setup-forced", "button-hold forces setup mode")
eq(h.setups, 1, "setup entered on button-hold")
eq(h.joins, 0, "button-hold skips the join attempt")
eq(h.leds[#h.leds], "setup", "button-hold shows the setup LED")

-- no creds -> setup --------------------------------------------------------

eq(provision.boot(harness{cfg = nil}) , "setup-noconf", "blank config -> setup")
h = harness{cfg = {ssid = "", fails = 0}}
eq(provision.boot(h), "setup-noconf", "empty-ssid config -> setup")
eq(h.joins, 0, "no-creds path never joins")
eq(h.setups, 1, "no-creds path enters setup")

-- strikes already maxed -> setup without a join ----------------------------

h = harness{cfg = CREDS(3), max_fails = 3, join_ok = true}
eq(provision.boot(h), "setup-strikes", "maxed strikes -> setup")
eq(h.joins, 0, "maxed strikes skips the join")
eq(h.setups, 1, "maxed strikes enters setup")

-- successful join clears a nonzero strike counter --------------------------

h = harness{cfg = CREDS(2), join_ok = true}
eq(provision.boot(h), "joined", "good creds join")
eq(h.joined_with.ssid, "Home", "join used the stored ssid")
eq(#h.saves, 1, "a successful join with prior strikes rewrites the counter")
eq(h.saves[1].fails, 0, "successful join clears strikes to 0")
eq(h.leds[#h.leds], "connected", "successful join shows the connected LED")
ok(h.leds[1] == "connecting", "join is preceded by the connecting LED")

-- successful join with no prior strikes writes nothing ---------------------

h = harness{cfg = CREDS(0), join_ok = true}
eq(provision.boot(h), "joined", "join with zero strikes")
eq(#h.saves, 0, "no needless flash write when strikes were already 0")

-- failed join below the limit: strike++, retry -----------------------------

h = harness{cfg = CREDS(0), reason = "auth", max_fails = 3}
r = select(1, provision.boot(h))
eq(r, "retry", "a failed join below the limit asks for a retry")
eq(select(2, provision.boot(harness{cfg = CREDS(0), reason = "auth"})),
   "auth", "boot returns the failure reason")
eq(#h.saves, 1, "failed join persists a strike")
eq(h.saves[1].fails, 1, "strike incremented 0 -> 1")
eq(h.setups, 0, "one failure does not enter setup")
eq(h.leds[#h.leds], "authfail", "auth failure shows the red LED")

-- failed join that reaches the limit: strike++ then setup ------------------

h = harness{cfg = CREDS(2), reason = "auth", max_fails = 3}
r = provision.boot(h)
eq(r, "setup-strikes", "the failure that reaches max strikes enters setup")
eq(h.saves[1].fails, 3, "strike incremented 2 -> 3 before the fallback")
eq(h.setups, 1, "reaching max strikes enters setup")
eq(h.leds[#h.leds], "setup", "the setup LED wins on the final strike")

-- a non-auth failure keeps amber (no false red) ----------------------------

h = harness{cfg = CREDS(0), reason = "notfound", max_fails = 3}
provision.boot(h)
eq(h.leds[#h.leds], "connecting", "a notfound failure keeps amber, not red")

-- the LED vocabulary is fully defined --------------------------------------

for _, s in ipairs({"setup", "connecting", "authfail", "connected"}) do
  ok(provision.LED[s] and provision.LED[s][1] == "nose",
     "LED vocabulary defines " .. s .. " on the nose")
end
eq(provision.LED.authfail[2] > 0, true, "authfail LED has red")
eq(provision.LED.connected[3] > 0, true, "connected LED has green")
