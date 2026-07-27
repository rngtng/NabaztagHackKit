-- net.dns: the captive-portal responder. Queries built by an independent
-- helper here (labels + header, not derived from the module); answers asserted
-- on concrete bytes (the AP IP in the rdata), per the repo testing rule.

local dns = net.dns
local IP = net.link.ip("192.168.0.1")

-- independent DNS query builder (matches RFC 1035 wire format) ---------------
local function qname(host)
  local out = {}
  for label in (host .. "."):gmatch("([^.]+)%.") do
    out[#out + 1] = string.char(#label) .. label
  end
  return table.concat(out) .. "\0"
end
local function query(id, host, qtype)
  return id .. string.pack(">I2I2I2I2I2", 0x0100, 1, 0, 0, 0) -- RD, 1 question
         .. qname(host) .. string.pack(">I2I2", qtype, 1)     -- qtype, IN
end

-- parse_query -----------------------------------------------------------------

local pq = dns.parse_query(query(H"1234", "captive.apple.com", 1))
eq(pq and pq.qtype, 1, "parse_query reads the A qtype")
eq(pq.id, H"1234", "parse_query keeps the id")
eq(pq.question, qname("captive.apple.com") .. string.pack(">I2I2", 1, 1),
   "parse_query spans the whole question")
eq(dns.parse_query("short"), nil, "a too-short packet is not a query")
-- a datagram with QR set is a response, not a query
local resp = H"0003" .. string.pack(">I2I2I2I2I2", 0x8000, 1, 0, 0, 0)
             .. qname("a.com") .. string.pack(">I2I2", 1, 1)
eq(dns.parse_query(resp), nil, "a packet with QR set is rejected")

-- server: an A query resolves to the AP IP -----------------------------------

local s = dns.server(IP)
local r = s:input(query(H"1234", "captive.apple.com", 1))
eq(r:sub(1, 2), H"1234", "response echoes the query id")
local flags, qd, an, ns, ar = string.unpack(">I2I2I2I2I2", r, 3)
eq(flags, 0x8180, "response flags are QR+RD+RA")
eq(qd, 1, "the question is echoed")
eq(an, 1, "an A query yields one answer")
eq(ns, 0, "no authority records")
eq(ar, 0, "no additional records")
eq(r:sub(13, 12 + #pq.question), pq.question, "question echoed verbatim")

-- answer = pointer-to-question, A, IN, ttl, rdata=IP (the last 16 bytes)
local ans = r:sub(-16)
local name, atype, aclass, ttl, rdlen = string.unpack(">I2I2I2I4I2", ans)
eq(name, 0xC00C, "answer name is a compression pointer to the question")
eq(atype, 1, "answer type is A")
eq(aclass, 1, "answer class is IN")
eq(ttl, 60, "answer ttl")
eq(rdlen, 4, "A rdata length is 4")
eq(ans:sub(-4), IP, "the A record resolves to the AP IP")

-- a different hostname resolves to the same IP (it is a sinkhole) -------------
eq(s:input(query(H"00ff", "connectivitycheck.gstatic.com", 1)):sub(-4), IP,
   "every hostname resolves to the AP IP")

-- non-A queries get NOERROR with no records (client falls back to A) ----------
local r6 = s:input(query(H"0001", "example.com", 28)) -- AAAA
local _, _, an6 = string.unpack(">I2I2I2", r6, 3)
eq(an6, 0, "an AAAA query gets no answer records")
eq(#r6, 12 + #qname("example.com") + 4, "AAAA response is header + question only")

eq(s:input("garbage"), nil, "an unparseable query is dropped")
