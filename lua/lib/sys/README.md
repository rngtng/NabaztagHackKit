# lua/lib/sys — the Lua system layer (#259)

What the rabbit knows about itself that is not a peripheral. Today that is
**the time of day**, which the lua track had no notion of at all: `nab.time()`
is milliseconds since boot and nothing more, so nothing could name a date —
no clock face, no scheduled behaviour, no timestamped log line, no HTTP
`Date`/`If-Modified-Since`, no cache expiry.

Mirrors `mtl/lib/sys/` (`time.mtl` + `timezones.mtl` + `net/ntp.mtl`). Per
design principle 1 there is **no C change here** — `net.udp` and the
`iface.udp_ports` demux already carried everything an SNTP client needs, and
everything else is integer arithmetic. Compiled off-device and shipped as
`#LC` frames over the REPL, so **nothing in this folder costs flash** (freezing
a boot subset is #219's call).

## Modules

| Module | Provides |
|--------|----------|
| `ntp.lua` | SNTP v4 client packets (RFC 4330): a 48-byte request, and the transmit timestamp out of a reply |
| `time.lua` | the wall clock: NTP-set epoch anchored to the device tick, civil dates, strftime-subset formatting, timezone offset + DST rule |

## Getting a time

```lua
sys.time.set(ifc:ntp(net.link.ip("192.168.0.1")))   -- one SNTP round trip
sys.time.zone(60, "EU")                             -- Berlin
print(sys.time.format(sys.time.now()))              -- 2026-08-01T12:34:56Z
print(sys.time.format(sys.time.localtime(), sys.time.HUMAN))
                                                    -- 2026-08-01 14:34:56
```

`ifc:ntp(server [, timeout])` lives in `net/iface.lua` (it is a UDP flow, and
net owns the socket), shaped exactly like `ifc:resolve`: one datagram to port
123 from an ephemeral port, retried on a 1 s timer until the timeout
(default 5 s), returning `unix seconds | nil, err`. `server` is a 4-byte
address or a name — a name goes through `ifc:resolve` first, so
`ifc:ntp("pool.ntp.org")` works now that #232 has landed. It returns **one**
value on purpose, so `sys.time.set(ifc:ntp(...))` cannot mis-bind a second
return onto `set`'s tick argument.

Net never touches the clock and sys never touches a socket: the reading is
handed back to the caller, the same pull-style split the rest of the stack
uses. `sys.ntp` is a soft dependency of `iface` — `ifc:ntp` says
`sys.ntp not loaded` rather than failing to load.

**Replies are not trusted.** The request plants an 8-byte cookie in its
transmit timestamp, **re-rolled on every attempt** (exactly as `:resolve`
re-rolls its transaction id); a reply must echo the current one in the
originate field, or it is a late answer to a superseded attempt — carrying a
time that is by now wrong — or a spoof, and the loop keeps waiting. Mode,
version, stratum and the leap-indicator alarm are all checked; a
kiss-of-death packet or an unsynchronised server is a **definitive** refusal
that ends the call immediately instead of retrying to the timeout.

`set` refuses anything that is not whole seconds, so the one-liner above
degrades to a no-op if the sync failed. Take the two lines when you want to
see *why*:

```lua
local e, err = ifc:ntp(server)
if e then sys.time.set(e) else print("no time:", err) end
```

## The clock between syncs

`sys.time` stores one epoch plus the `nab.time()` tick it was anchored to, and
derives everything else. `now()` is that anchor plus the elapsed tick — which
makes the tick's own limits the interesting part:

`nab.time()` is `uint32` milliseconds since boot pushed as a `lua_Integer`,
which under `LUA_32BITS` is **32-bit signed** — it goes negative after ~24.8
days and wraps to 0 after ~49.7 (the second half of #259's premise). So:

- differences are taken in that same wrap arithmetic, where `tick - anchor` is
  exact modulo 2^32;
- the anchor is **advanced on every read** (whole seconds only, the remainder
  is kept), so neither rollover is ever more than one read away, and the clock
  survives both indefinitely;
- a gap wider than a signed difference can express (>24.8 days between reads)
  comes back negative — the clock then **holds** its last value rather than
  leaping backwards, and the next sync repairs it.

`due()` says when to re-sync (`RESYNC`, 1 h — the 1 ms tick comes off the CPU
clock, not a crystal-trimmed RTC, so it drifts). Pull-style like everything
else: the app polls it and calls `ifc:ntp` itself, so nothing here owns a timer
or a socket.

```lua
if sys.time.due() then sys.time.set(ifc:ntp(server)) end   -- in the app loop
```

## Epochs, eras and 2038

Unix seconds are what everything speaks; NTP counts from 1900, 2208988800 s
earlier. That constant **does not fit** a 32-bit signed integer, so the
conversion is deliberately mod-2^32 — and that is exactly why the NTP **era-1
rollover** on 2036-02-07 needs no special case: the server's counter wraps,
our subtraction wraps, and they cancel. (`ntp.parse` gives up the one second
where the transmit timestamp is exactly zero, which is indistinguishable from
a server that never filled the field; the caller retries.)

The real cliff is **2038-01-19T03:14:07Z**, where Unix seconds stop fitting a
32-bit signed `lua_Integer`. The representable window is 1901-12-13 .. that
instant, and both ends are covered by tests.

## Dates without `os.date`

The device registers `base` + `table` + `string` + `nab` only, so the calendar
is ours: Howard Hinnant's `days_from_civil`/`civil_from_days` pair, on a
March-based year so the leap day lands last and no month-length table is
needed. Lua's `//` is floor division, which is what the algorithm wants, so
the negative-era correction the C++ original carries is not repeated —
and negative epochs (pre-1970) come out right for free.

```lua
sys.time.date(epoch)   --> {year,month,day,hour,min,sec,wday,yday}  (wday 1=Sun)
sys.time.epoch(t)      --> the inverse; normalises out-of-range fields
sys.time.format(e[,f]) --> strftime subset: %Y %y %m %d %H %M %S %j %a %b %%
```

`ISO` (the default), `HTTP` (RFC 1123, for `Date`/`If-Modified-Since`) and
`HUMAN` are named formats; an unknown spec is left verbatim.

## Timezones: a rule, not a table

`mtl/lib/sys/timezones.mtl` is a 114-entry city table that still cannot say
whether summer time is running. Two rules cover everywhere a rabbit is
plausibly plugged in, and the base offset is a single number (a config key is
#268's business):

- **`EU`** — last Sunday of March 01:00 UTC to last Sunday of October 01:00
  UTC. The whole union switches on the same UTC instant.
- **`US`** — second Sunday of March to first Sunday of November, both at 02:00
  local *standard* time; November's 02:00 is quoted in daylight time, so it is
  01:00 standard. Compared in standard time, hence the offset argument.

```lua
sys.time.zone(60, "EU")      -- Berlin        sys.time.zone(-300, "US")  -- NY
sys.time.zone(330)           -- Delhi: +5:30, no summer time
sys.time.offset(epoch)       -- minutes east of UTC at that instant, DST in
sys.time.localtime([epoch])  -- the epoch shifted into local time, for format()
```

Southern-hemisphere summer time is not modelled — set the offset and leave the
rule off. `localtime()` deliberately returns a *local-clock reading*, not a
Unix timestamp; feed it to `date()`/`format()`, never back into `set()`.

## Tests

`task lua:lib:test` runs `test/run.lua` under the `tools/luac` host `lua` (same
vendored tree + `LUA_32BITS` `luaconf.h` as the device, so the 32-bit tick and
epoch wrap exactly as they do on the rabbit). 221 assertions.

Fixtures come from an **independent** Python generator (`scratchpad
ntpfix.py`): SNTP packets from `struct.pack` straight off RFC 4330's field
layout, civil dates and RFC 1123 strings from CPython's `datetime`, and every
DST instant from the IANA tzdb via `zoneinfo` — none of it derived from the
modules under test. The scripted server that drives `ifc:ntp` is written in the
test file and **pinned to the generator's bytes** before it is used, so it
cannot drift into agreeing with `ntp.lua`.

Covered: the request bytes; every reply the parser must refuse (short, our own
request looped back, NTPv2, LI 3, stratum 0/16, an unset transmit timestamp, a
mismatched cookie); the era-1 rollover; ten independently-computed civil dates
including both leap rules in range, the 1969 and 2038 ends, and a round trip
back through `epoch()`; the format specs; the clock across the signed-tick
rollover, the uint32 wrap and a stall; the resync policy; and both DST rules at
their exact 2026 and 2027 transition seconds. The `ifc:ntp` flow is driven end
to end — success, a stale-cookie reply skipped and the retry accepted, a
definitive refusal stopping early, a timeout, and the port unregistered on
every path.

`task lua:lib:size` (as of #259): `sys/ntp` 928 B, `sys/time` 3774 B.

## On device

Verified as device bytecode on the emulated ARM7 (`task lua:apps:simulate` with
the two modules plus a driver): the era-1 conversion, both DST rules and the
formatter produce the same values there as on the host —
`2036-02-07T07:28:16Z`, `2026-08-01 14:34:56` (Berlin) and `08:34:56`
(New York) off the same epoch.

The hardware round trip (join → `ifc:ntp` → print the real wall-clock time on
the JTAG rig, see the `hw-flash-repl` skill) is #259's remaining DoD item.
