-- net.dns - a captive-portal DNS responder (#233 follow-up). Not a resolver:
-- one canned answer. Every A query gets the AP's own IP, so a phone that joins
-- the open setup AP resolves whatever hostname its OS probes
-- (captive.apple.com, connectivitycheck.gstatic.com, msftconnecttest.com, ...)
-- to the rabbit, fetches the config page instead of the expected "success"
-- response, and pops the "Sign in to network" sheet straight onto the portal -
-- no typing 192.168.0.1 into a browser.
--
-- The DHCP server already hands the client this box as its DNS server (#217,
-- dhcp.server opt 6 = self), so those probe lookups arrive here on UDP:53.
--
-- Pure Lua over string.pack like its siblings; iface:dnsd wires it to the port.

net = net or {}
local dns = {}
net.dns = dns

local A = 1  -- DNS TYPE=A (IPv4 address)

-- Parse just enough of a query to echo it and branch on the type. We do not
-- decode the QNAME to a string (we answer every name the same) - only span it.
-- -> {id=<2 bytes>, question=<raw QNAME+QTYPE+QCLASS>, qtype=} | nil
function dns.parse_query(p)
  if #p < 12 then return nil end
  local flags, qd = string.unpack(">I2I2", p, 3)
  if (flags & 0x8000) ~= 0 then return nil end -- a response, not a query
  if qd < 1 then return nil end
  local i = 13                                 -- QNAME starts after the header
  while true do
    local len = p:byte(i)
    if len == nil then return nil end
    if len == 0 then i = i + 1; break end      -- root label ends the name
    if len >= 0xC0 then return nil end          -- no compression in a query name
    i = i + 1 + len
    if i > #p then return nil end
  end
  if i + 3 > #p then return nil end            -- need QTYPE(2)+QCLASS(2)
  return {id = p:sub(1, 2), question = p:sub(13, i + 3),
          qtype = string.unpack(">I2", p, i)}
end

-- s = dns.server(ip)  (ip = 4-byte binary address, e.g. net.link.ip("192.168.0.1"))
-- s:input(query_payload) -> response_payload | nil
-- An A query is answered with `ip`; any other type gets NOERROR with no records
-- (so the client falls back to the A lookup).
function dns.server(ip)
  local s = {ip = ip}
  function s:input(query)
    local q = dns.parse_query(query)
    if not q then return nil end
    local answer, ancount = "", 0
    if q.qtype == A then
      -- name = pointer to the question at offset 12 (0xC00C); A/IN; TTL 60; 4-byte rdata
      answer = string.pack(">I2I2I2I4I2", 0xC00C, A, 1, 60, 4) .. self.ip
      ancount = 1
    end
    -- flags 0x8180: response, recursion-desired echoed, recursion-available
    local header = q.id .. string.pack(">I2I2I2I2I2", 0x8180, 1, ancount, 0, 0)
    return header .. q.question .. answer
  end
  return s
end
