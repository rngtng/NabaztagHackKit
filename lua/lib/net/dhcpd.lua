-- net.dhcp.server - the single-lease DHCP server for AP config mode (#218/#233),
-- split out of dhcp.lua for #219.
--
-- A rabbit that is *joining* never hands out a lease, so none of this is on the
-- boot path and none of it is frozen into flash. It extends the existing
-- net.dhcp table rather than making a table of its own, so every caller
-- (iface_x's :dhcpd) keeps working unchanged.
--
-- Load order: after dhcp.lua.

net = net or {}
local dhcp = net.dhcp
local link = net.link

local ANY = "\0\0\0\0"

-- Server for AP config mode: one fixed lease, DNS pointed at ourselves so
-- the phone's first lookup lands on the config portal (#218). A REQUEST for
-- any other address is NAKed back to rediscovery.
-- o = {ip=, client_ip=[, mask=]} -> s; s:input(udp:67 payload) -> frame|nil
function dhcp.server(o)
  local s = {ip = o.ip, client_ip = o.client_ip,
             mask = o.mask or link.ip("255.255.255.0")}

  function s:input(dgram)
    local r = dhcp.parse(dgram)
    if not r or r.op ~= 1 or not r.msgtype then return nil end
    local reply
    if r.msgtype == dhcp.DISCOVER then
      reply = dhcp.OFFER
    elseif r.msgtype == dhcp.REQUEST then
      local want = r.opts[50] or r.ciaddr
      reply = (want == self.client_ip or want == ANY) and dhcp.ACK or dhcp.NAK
    else
      return nil
    end
    local body
    if reply == dhcp.NAK then
      body = dhcp.build{op = 2, xid = r.xid, mac = r.mac, msgtype = dhcp.NAK,
                        opts = {{54, self.ip}}}
    else
      body = dhcp.build{op = 2, xid = r.xid, mac = r.mac, msgtype = reply,
                        yiaddr = self.client_ip, siaddr = self.ip,
                        opts = {{54, self.ip}, {51, string.pack(">I4", 86400)},
                                {1, self.mask}, {3, self.ip}, {6, self.ip}}}
    end
    return dhcp.bcast_frame(self.ip, 67, 68, body)
  end
  return s
end
