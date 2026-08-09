-- net.link's address conveniences (#217), split out of link.lua for #219: the
-- asserting parse and the two human-readable renderings.
--
-- Nothing on the boot path calls them - netboot needs only link.aton, the
-- non-throwing form, to turn the configured dotted-quad boot server into an
-- address - so they are not frozen into flash. They extend the existing
-- net.link table, so every caller keeps working unchanged.
--
-- Load order: after link.lua.

net = net or {}
local link = net.link

function link.ip(s)
  return assert(link.aton(s), "bad IPv4 address")
end

function link.ntoa(ip)
  return ("%d.%d.%d.%d"):format(ip:byte(1, 4))
end

function link.mac2s(m)
  return (m:gsub(".", function(c) return ("%02x:"):format(c:byte()) end)
          :sub(1, -2))
end
