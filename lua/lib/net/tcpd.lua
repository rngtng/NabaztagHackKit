-- net.tcp's listening half (#217), split out of tcp.lua for #219: opening a
-- passive socket and accepting the first SYN.
--
-- Fetching one file at boot only ever *originates* a connection, so none of
-- this is on the boot path and none of it is frozen into flash. It extends the
-- existing net.tcp table, so every caller (iface_x's :serve) keeps working
-- unchanged.
--
-- Load order: after tcp.lua.

net = net or {}
local tcp = net.tcp
local new, arm, MSS = tcp.new, tcp.arm, tcp.MSS

-- {src=,port=[,iss=,clock=]} -> conn in "listen"; accepts the first SYN
function tcp.listen(o)
  local c = new{src = o.src, sport = o.port, iss = o.iss, clock = o.clock}
  c.state = "listen"
  return c
end

-- The "listen" branch of tcp's :input, reached only through the tcp.accept
-- hook there - so tcp.lua carries the (shared) port and checksum filtering and
-- this carries the handshake. c is the listening connection, s the parsed
-- segment, pkt the IPv4 packet it came in, out the caller's packet array.
function tcp.accept(c, s, pkt, out)
  if (s.flags & tcp.SYN) == 0 or (s.flags & tcp.ACK) ~= 0 then return end
  c.dst, c.dport = pkt.src, s.sport
  c.rcv_nxt = s.seq + 1
  if s.mss and s.mss < MSS then c.mss = s.mss else c.mss = MSS end
  c.iss = c.iss or (c.clock() * 31 + 5) & 0x7FFFFFFF
  c.snd_nxt = c.iss
  c.state = "syn-received"
  arm(c, out, tcp.SYN | tcp.ACK, "", string.pack(">BBI2", 2, 4, 1460))
end
