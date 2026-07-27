-- net.dns: the resolver (#232) and the captive-portal responder. Queries built
-- by an independent helper here (labels + header, not derived from the
-- module); resolver fixtures are whole packets from an independent Python
-- generator (scratchpad dnsfix.py, struct.pack straight off RFC 1035).
-- Everything is asserted on concrete bytes - the address in the rdata, the
-- TTL, the error string - per the repo testing rule.

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

-- resolver: fixtures from the independent Python generator --------------------

local QUERY = H[[12340100000100000000000007657861
                6d706c6503636f6d0000010001]]
local RESP = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c00045db8d822]]
local RESP_FULL = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001076578
                616d706c6503636f6d00000100010000
                012c00045db8d822]]
local RESP_CNAME = H[[beef8180000100020000000003777777
                076578616d706c6503636f6d00000100
                01c00c0005000100000e100002c010c0
                2d000100010000003c000401020304]]
local RESP_AAAA_FIRST = H[[12348180000100020000000007657861
                6d706c6503636f6d0000010001c00c00
                1c00010000003c001000010203040506
                0708090a0b0c0d0e0fc00c0001000100
                00003c00040a000007]]
local RESP_NXDOMAIN = H[[12348183000100000000000007657861
                6d706c6503636f6d0000010001]]
local RESP_EMPTY = H[[12348180000100000000000007657861
                6d706c6503636f6d0000010001]]
local RESP_TC = H[[12348380000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c00045db8d822]]
local RESP_OTHER_Q = H[[12348180000100010000000004657669
                6c076578616d706c650000010001c00c
                000100010000012c000406060606]]
local RESP_BADID = H[[99998180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c000406060606]]
local RESP_QR0 = H[[12340100000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c00045db8d822]]
local RESP_SHORT_RDATA = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c00045db8]]
local RESP_A_RDLEN16 = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000012c001000010203040506
                0708090a0b0c0d0e0f]]
local RESP_ANCOUNT_LIES = H[[12348180000100030000000007657861
                6d706c6503636f6d0000010001c00c00
                0500010000012c0002c00c]]
local RESP_RUNAWAY_LABEL = H[[12348180000100010000000007657861
                6d706c6503636f6d00000100013f6368
                6f70706564]]
local RESP_WRONG_CLASS = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100030000012c000409090909]]
local RESP_TTL0 = H[[12348180000100010000000007657861
                6d706c6503636f6d0000010001c00c00
                0100010000000000045db8d822]]

local ID = H"1234"
local EXAMPLE_IP = H"5db8d822" -- 93.184.216.34

-- qname encoding --------------------------------------------------------------

eq(dns.qname("example.com"), "\7example\3com\0", "qname label encoding")
eq(dns.qname("example.com."), "\7example\3com\0", "a trailing root dot is fine")
eq(dns.qname("a"), "\1a\0", "single-label name")
eq(dns.qname(""), nil, "the empty name is rejected")
eq(dns.qname("a..b"), nil, "an empty label is rejected")
eq(dns.qname(".com"), nil, "a leading dot is rejected")
eq(dns.qname(("x"):rep(64) .. ".com"), nil, "a label over 63 bytes is rejected")
eq(dns.qname(("x"):rep(63) .. ".com"), "\63" .. ("x"):rep(63) .. "\3com\0",
   "a 63-byte label is the maximum and is accepted")
eq(dns.qname((("x"):rep(60) .. "."):rep(5)), nil, "a name over 253 bytes is rejected")
eq(dns.qname(nil), nil, "a non-string name is rejected")

-- query build (byte-for-byte against the generator) ---------------------------

eq(dns.query(ID, "example.com"), QUERY, "query matches the fixture bytes")
local qflags, qqd, qan = string.unpack(">I2I2I2", QUERY, 3)
eq(qflags, 0x0100, "query asks for recursion")
eq(qqd, 1, "query carries exactly one question")
eq(qan, 0, "query carries no records")
eq(dns.query(H"abcd", "example.com"):sub(1, 2), H"abcd", "the id is ours")
eq(dns.query(ID, "a..b"), nil, "a malformed name yields no query")
eq(select(2, dns.query(ID, "a..b")), "bad name", "and says why")

-- answer parse: the happy paths ----------------------------------------------

local ip, ttl = dns.answer(RESP, ID, "example.com")
eq(ip, EXAMPLE_IP, "compressed answer resolves to 93.184.216.34")
eq(ttl, 300, "answer ttl is 300 s")
eq(ip and net.link.ntoa(ip), "93.184.216.34", "and prints as the expected quad")

ip, ttl = dns.answer(RESP_FULL, ID, "example.com")
eq(ip, EXAMPLE_IP, "an uncompressed owner name parses the same")
eq(ttl, 300, "uncompressed answer ttl")

-- CNAME chain: skip the CNAME (whose rdata is itself a pointer), take the A
ip, ttl = dns.answer(RESP_CNAME, H"beef", "www.example.com")
eq(ip, H"01020304", "a CNAME chain resolves to the A record behind it")
eq(ttl, 60, "the A record's ttl, not the CNAME's 3600")

ip = dns.answer(RESP_AAAA_FIRST, ID, "example.com")
eq(ip, H"0a000007", "an AAAA record before the A is skipped")

-- answer parse: everything that must be refused --------------------------------

local function refused(p, host, label, id)
  local a, err = dns.answer(p, id or ID, host or "example.com")
  eq(a, nil, label)
  ok(type(err) == "string" and #err > 0, label .. " reports why")
  return err
end

eq(refused(RESP_NXDOMAIN, nil, "NXDOMAIN is not an address"), "rcode 3",
   "the rcode is reported")
eq(refused(RESP_EMPTY, nil, "NOERROR with no records"), "no address",
   "an empty answer section says no address")
eq(refused(RESP_TC, nil, "a truncated (TC) response"), "truncated",
   "TC is refused - there is no TCP fallback")
eq(refused(RESP_OTHER_Q, nil, "an answer to someone else's question"),
   "question mismatch", "the echoed question is checked")
eq(refused(RESP_BADID, nil, "a response with the wrong id"), "wrong id",
   "the transaction id is checked")
eq(refused(RESP_QR0, nil, "a packet with QR clear"), "not a response",
   "QR must be set")
refused(RESP_SHORT_RDATA, nil, "rdata running past the end of the packet")
eq(refused(RESP_A_RDLEN16, nil, "an A record with a 16-byte rdata"),
   "no address", "a bogus A rdlength is skipped, not trusted")
refused(RESP_ANCOUNT_LIES, nil, "an ancount larger than the records present")
refused(RESP_RUNAWAY_LABEL, nil, "a label length running off the end")
eq(refused(RESP_WRONG_CLASS, nil, "an A record in the wrong class"),
   "no address", "class CH is not our answer")
refused("", nil, "an empty datagram")
refused("garbage", nil, "a datagram far too short to be a header")
refused(RESP .. ("\0"):rep(600), nil, "an oversized (>512 B) datagram")
refused(RESP, "other.example", "a response for a name we did not ask")
refused(RESP, "a..b", "a response checked against a malformed name")

-- A pointer loop must terminate, not hang: the answer's owner name sits at
-- offset 29 and is a pointer to offset 29 - itself.
local LOOP = H"12348180000100010000000007657861"
             .. H"6d706c6503636f6d0000010001" .. H"c01d"
             .. string.pack(">I2I2I4I2", 1, 1, 60, 4) .. H"01010101"
ip = dns.answer(LOOP, ID, "example.com")
eq(ip, H"01010101", "a self-referential pointer is spanned, never followed")

-- cache -----------------------------------------------------------------------

dns.forget()
eq(dns.cached("example.com", 1000), nil, "a cold cache misses")
dns.remember("example.com", EXAMPLE_IP, 300, 1000)
eq(dns.cached("example.com", 1000), EXAMPLE_IP, "a stored entry hits")
eq(dns.cached("example.com", 300999), EXAMPLE_IP, "and is live 1 ms before ttl")
eq(dns.cached("example.com", 301000), nil, "and is dead at ttl (300 s)")
eq(dns.cached("other.com", 1000), nil, "another name still misses")

dns.forget()
eq(dns.n, 0, "forget empties the entry count")
dns.remember("ttl0.example", EXAMPLE_IP, 0, 1000)
eq(dns.cached("ttl0.example", 1000), nil, "a ttl-0 answer is never cached")
eq(dns.n, 0, "and does not consume a cache slot")
eq(select(2, dns.answer(RESP_TTL0, ID, "example.com")), 0,
   "the ttl-0 fixture really does carry ttl 0")
eq(dns.answer(RESP_TTL0, ID, "example.com"), EXAMPLE_IP,
   "a ttl-0 answer is still a usable address")

dns.remember("clamp.example", EXAMPLE_IP, 999999, 0)
eq(dns.cached("clamp.example", dns.MAX_TTL * 1000 - 1), EXAMPLE_IP,
   "an absurd ttl is clamped to MAX_TTL, not overflowed")
eq(dns.cached("clamp.example", dns.MAX_TTL * 1000), nil,
   "and expires exactly there")

-- bounded (#251): the cap clears wholesale, and the newest entry survives
dns.forget()
for k = 1, dns.MAX do
  dns.remember("h" .. k .. ".example", string.char(10, 0, 0, k), 300, 0)
end
eq(dns.n, dns.MAX, "the cache fills to MAX")
eq(dns.cached("h1.example", 0), H"0a000001", "the first entry is still there")
dns.remember("overflow.example", H"0a0000ff", 300, 0)
eq(dns.n, 1, "one entry past MAX clears the table")
eq(dns.cached("h1.example", 0), nil, "the old entries are gone")
eq(dns.cached("overflow.example", 0), H"0a0000ff", "the new entry is kept")
dns.remember("overflow.example", H"0a0000fe", 300, 0)
eq(dns.n, 1, "refreshing an existing entry is not a new insert")
eq(dns.cached("overflow.example", 0), H"0a0000fe", "and updates the address")
dns.forget()
