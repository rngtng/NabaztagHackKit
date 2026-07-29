-- net.dns - both DNS halves, sharing one wire format:
--
--   * the resolver (#232): A-record query build + response parse, plus a
--     bounded TTL cache. Transport-free and pull-style like dhcp.lua - the
--     caller (iface:resolve) owns the socket, the clock and the retry timer.
--     No failover, no periodic refresh, no record type but A: a hostname in
--     the boot URL is the whole point, and the mtl reference's 504 lines of
--     multi-server machinery buy nothing on one rabbit with one resolver.
--   * the captive-portal responder (#233 follow-up): one canned answer. Every
--     A query gets the AP's own IP, so a phone that joins the open setup AP
--     resolves whatever hostname its OS probes (captive.apple.com,
--     connectivitycheck.gstatic.com, msftconnecttest.com, ...) to the rabbit,
--     fetches the config page instead of the expected "success" response, and
--     pops the "Sign in to network" sheet straight onto the portal - no typing
--     192.168.0.1 into a browser. The DHCP server already hands the client
--     this box as its DNS server (#217, dhcp.server opt 6 = self), so those
--     probe lookups arrive here on UDP:53.
--
-- Pure Lua over string.pack like its siblings; iface:resolve / iface:dnsd wire
-- them to the port.

net = net or {}
local dns = {}
net.dns = dns

local A, IN = 1, 1     -- DNS TYPE=A (IPv4 address), CLASS=IN
local MAX_MSG = 512    -- RFC 1035 UDP limit; we never announce EDNS0

-- resolver: names ------------------------------------------------------------

-- "example.com" -> "\7example\3com\0"; nil for anything not a legal name, so
-- a caller can never hand a length byte we did not measure ourselves to the
-- wire. One trailing root dot is tolerated ("example.com.").
function dns.qname(host)
  if type(host) ~= "string" then return nil end
  host = host:gsub("%.$", "")
  if #host == 0 or #host > 253 then return nil end
  local out = {}
  for label in (host .. "."):gmatch("([^.]*)%.") do
    if #label == 0 or #label > 63 then return nil end
    out[#out + 1] = string.char(#label) .. label
  end
  return table.concat(out) .. "\0"
end

-- resolver: query / response --------------------------------------------------

-- id = 2 raw bytes (a fresh transaction id per attempt).
-- -> query payload | nil, err
function dns.query(id, host)
  local q = dns.qname(host)
  if not q then return nil, "bad name" end
  -- flags 0x0100: standard query, recursion desired; one question, no records
  return id .. string.pack(">I2I2I2I2I2", 0x0100, 1, 0, 0, 0)
         .. q .. string.pack(">I2I2", A, IN)
end

-- Span a (possibly compressed) name and return the offset just past it. Real
-- servers compress the answer's owner name to a 0xC0 pointer at the question,
-- so this is not optional. We never *follow* a pointer - a pointer always ends
-- the name, and refusing to chase it means a crafted pointer loop cannot hang
-- the parser. nil = malformed.
local function skip_name(p, i)
  while true do
    local len = p:byte(i)
    if len == nil then return nil end
    if len >= 0xC0 then return i + 1 <= #p and i + 2 or nil end -- pointer (2 B)
    if len >= 0x40 then return nil end                          -- reserved
    if len == 0 then return i + 1 end                           -- root label
    i = i + 1 + len
  end
end

-- Every length here comes off the wire, so each one is bounds-checked against
-- #p before it is used; dns.answer runs this under pcall as the backstop.
--
-- A failure carries a third value, `definitive`: true once the datagram is
-- confidently the answer to THIS query (our id matched) but is negative or
-- unusable - NXDOMAIN, TC, a malformed answer section, no A record. The waiter
-- (iface:resolve) must stop on those, not retry to its full timeout. A false
-- `definitive` marks a datagram we are not sure is ours - wrong id, a spoofed
-- question echo, a runt/oversized frame - which the waiter ignores and keeps
-- listening past, so a stray packet cannot cut a lookup short.
local function parse_answer(p, id, host)
  if #p < 12 then return nil, "short response", false end
  if #p > MAX_MSG then return nil, "oversized response", false end
  if p:sub(1, 2) ~= id then return nil, "wrong id", false end
  local flags, qd, an = string.unpack(">I2I2I2", p, 3)
  if (flags & 0x8000) == 0 then return nil, "not a response", false end
  -- Past the id check the datagram is ours: negatives below are definitive.
  if (flags & 0x0200) ~= 0 then return nil, "truncated", true end -- no TCP fallback
  if (flags & 0x000F) ~= 0 then return nil, "rcode " .. (flags & 0x000F), true end
  if qd ~= 1 then return nil, "bad question count", true end
  local qn = dns.qname(host)
  if not qn then return nil, "bad name", true end
  -- The echoed question must be the one we asked: cheap, and it drops a blind
  -- off-path answer. A mismatch is treated as not-ours (ignore, keep waiting),
  -- so a spoofed reply carrying our id but a different question cannot end the
  -- lookup early - the real answer (or the timeout) still decides.
  local q = qn .. string.pack(">I2I2", A, IN)
  if p:sub(13, 12 + #q) ~= q then return nil, "question mismatch", false end
  local i = 13 + #q
  for _ = 1, an do
    i = skip_name(p, i)
    if not i then return nil, "bad rr name", true end
    if i + 9 > #p then return nil, "short rr", true end
    local rtype, rclass, ttl, rdlen = string.unpack(">I2I2I4I2", p, i)
    i = i + 10
    if i + rdlen - 1 > #p then return nil, "bad rdlength", true end
    if rtype == A and rclass == IN and rdlen == 4 then
      return p:sub(i, i + 3), ttl
    end
    i = i + rdlen -- CNAME/AAAA/...: the A we want is further down the section
  end
  return nil, "no address", true
end

-- The answer to our own query, or an error - never a throw (principle 3).
-- -> 4-byte ip, ttl (seconds) | nil, err, definitive
-- `definitive` (see parse_answer) tells iface:resolve a negative that must stop
-- the retry loop from a datagram it should ignore and wait past.
function dns.answer(p, id, host)
  local ok, ip, ttl, definitive = pcall(parse_answer, p, id, host)
  if not ok then return nil, "malformed response", false end
  return ip, ttl, definitive
end

-- resolver: cache -------------------------------------------------------------

dns.cache = {}     -- [host] = {ip = <4 bytes>, exp = <ms deadline>}
dns.n = 0          -- entries in dns.cache; maintained by remember/forget
dns.MAX = 8        -- cap: a rabbit talks to a boot server, an NTP host, little else
dns.MAX_TTL = 3600 -- s; clamped so exp stays far inside the 32-bit ms clock

function dns.forget()
  dns.cache, dns.n = {}, 0
end

-- `now` is the caller's ms clock (nab.time). Expiry compares by signed
-- difference, so it survives the clock wrapping like tcp's sequence compare.
function dns.cached(host, now)
  local e = dns.cache[host]
  if e and now - e.exp < 0 then return e.ip end
end

function dns.remember(host, ip, ttl, now)
  if not ttl or ttl <= 0 then return end -- TTL 0 means use once, never store
  if ttl > dns.MAX_TTL then ttl = dns.MAX_TTL end
  if dns.cache[host] == nil then
    -- Bounded (#251), with arp.learn's crude cap-and-clear: recovery costs one
    -- query, and per-entry LRU links would cost more than the thing they guard.
    if dns.n >= dns.MAX then dns.forget() end
    dns.n = dns.n + 1
  end
  dns.cache[host] = {ip = ip, exp = now + ttl * 1000}
end

-- captive-portal responder ----------------------------------------------------

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
