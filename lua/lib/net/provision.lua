-- net.provision - the provisioning boot decision + join-with-fallback (#234,
-- M11e-2). The robustness half of provisioning: on every boot decide setup mode
-- vs. a join attempt, drive the LED vocabulary, and guarantee that a wrong-PSK
-- or vanished-AP rabbit falls back to setup mode (net.setup, #233) within a
-- bounded number of boots instead of wedging in a boot loop.
--
-- The transition is driven by a persisted consecutive-failure counter in the
-- config sector (nab.config's `fails`, #214/#234): each failed join bumps it,
-- a successful join clears it, and MAX_FAILS strikes force setup mode. The
-- update is a plain nab.config read-modify-write, so it is power-loss tolerant
-- by construction - a torn write leaves the sector blank/invalid, which the
-- next boot reads as "no creds" and enters setup, never a crash (worst case one
-- lost increment).
--
-- boot() is pure decision logic over injected hooks so it tests host-side;
-- run() wires the real nab.* + net.setup defaults. The boot-path integration
-- (calling run() in place of the M5 demo boot chunk) is #219's job - this
-- module ships as a lib, like net.setup.

net = net or {}
local provision = {}
net.provision = provision

-- Consecutive failed joins before we stop retrying and open setup mode.
provision.MAX_FAILS = 3

-- LED vocabulary (#234): the minimal provisioning slice of the #170 boot-
-- feedback table - nose LED only, ears/OTA rows are out of scope. {name,r,g,b}.
provision.LED = {
  setup = {"nose", 0, 0, 40}, -- dim blue: setup mode, waiting for creds
  connecting = {"nose", 40, 20, 0}, -- amber: joining the configured network
  authfail = {"nose", 60, 0, 0}, -- red: auth failed (wrong password?)
  connected = {"nose", 0, 40, 0}, -- green: joined
}

-- The boot decision. hooks h (all optional except join):
--   button_held() -> bool       head-button held at boot (the recovery hatch)
--   read_cfg()    -> cfg | nil   nab.config(): {ssid,psk,url,fails} or nil
--   save(cfg)                    nab.config(cfg): persist creds + fails
--   join(cfg)     -> ok, reason  attempt the join; reason is nab.wifi's tag
--   led(state)                   set the LED for a provision.LED state name
--   setup()                      net.setup.run(): blocks until provisioned
--   max_fails     = MAX_FAILS
--
-- Returns (result, reason): result is one of "setup-forced", "setup-noconf",
-- "setup-strikes", "joined", "retry"; reason is nab.wifi's failure tag on a
-- failed join, else nil. "retry" means the caller should reboot and let boot()
-- run again - bounded by max_fails, so it can never loop forever.
function provision.boot(h)
  local max = h.max_fails or provision.MAX_FAILS
  local function led(state) if h.led then h.led(state) end end
  local function enter_setup(why)
    led("setup")
    if h.setup then h.setup() end
    return why
  end

  if h.button_held and h.button_held() then
    return enter_setup("setup-forced")
  end

  local cfg = h.read_cfg and h.read_cfg()
  if not (cfg and cfg.ssid and cfg.ssid ~= "") then
    return enter_setup("setup-noconf")
  end
  if (cfg.fails or 0) >= max then
    return enter_setup("setup-strikes")
  end

  led("connecting")
  local ok, reason = h.join(cfg)
  if ok then
    if (cfg.fails or 0) ~= 0 then -- clear strikes; skip the write if already 0
      cfg.fails = 0
      if h.save then h.save(cfg) end
    end
    led("connected")
    return "joined"
  end

  -- Record a strike. reason "auth" (bad-PSK hint) gets the red LED; any other
  -- failure keeps the amber connecting LED up to the brief pre-reboot window.
  cfg.fails = (cfg.fails or 0) + 1
  if h.save then h.save(cfg) end
  if reason == "auth" then led("authfail") end
  if cfg.fails >= max then
    return enter_setup("setup-strikes"), reason
  end
  return "retry", reason
end

-- nab-backed wiring of boot(). opts.button_held / opts.max_fails override the
-- defaults (tests, or a different recovery gesture). Returns boot()'s result.
function provision.run(opts)
  opts = opts or {}
  return provision.boot{
    button_held = opts.button_held or nab.button,
    read_cfg = function() return nab.config() end,
    save = function(cfg) return nab.config(cfg) end,
    join = function(cfg)
      local ok, _, reason = nab.wifi(cfg.ssid, cfg.psk)
      return ok == true, reason
    end,
    led = function(state)
      local d = provision.LED[state]
      if d then nab.led(d[1], d[2], d[3], d[4]) end
    end,
    setup = function() return net.setup.run() end,
    max_fails = opts.max_fails,
  }
end
