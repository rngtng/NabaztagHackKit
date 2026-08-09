-- net.provision.run - the nab-backed wiring of provision.boot (#234), split out
-- of provision.lua for #219.
--
-- It wires `setup = net.setup.run`, and net.setup is not resident, so this is
-- the REPL/app wiring rather than the boot image's: boot/netboot.lua builds the
-- same hooks with a setup hook that signals the missing portal instead. The
-- decision itself (provision.boot) is shared and stays resident.
--
-- Load order: after provision.lua, plus net.setup for the setup hook.

net = net or {}
local provision = net.provision

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
