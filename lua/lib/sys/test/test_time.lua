-- sys.time: civil dates, formatting, the tick-anchored clock and the timezone
-- rules (#259).
--
-- Every expected date, weekday, day-of-year and RFC 1123 string below comes
-- from CPython's datetime, and every DST instant from the IANA tzdb via
-- zoneinfo (scratchpad ntpfix.py) - an independent implementation, not this
-- module's own output.

local time = sys.time

-- date(): epoch -> civil ------------------------------------------------------

-- {epoch, "iso", wday, yday, "rfc 1123"}
local DATES = {
  {0, "1970-01-01T00:00:00Z", 5, 1, "Thu, 01 Jan 1970 00:00:00 GMT"},
  {-1, "1969-12-31T23:59:59Z", 4, 365, "Wed, 31 Dec 1969 23:59:59 GMT"},
  {946684799, "1999-12-31T23:59:59Z", 6, 365, "Fri, 31 Dec 1999 23:59:59 GMT"},
  {951825600, "2000-02-29T12:00:00Z", 3, 60, "Tue, 29 Feb 2000 12:00:00 GMT"},
  {1709251199, "2024-02-29T23:59:59Z", 5, 60, "Thu, 29 Feb 2024 23:59:59 GMT"},
  {1767225600, "2026-01-01T00:00:00Z", 5, 1, "Thu, 01 Jan 2026 00:00:00 GMT"},
  {1785587696, "2026-08-01T12:34:56Z", 7, 213, "Sat, 01 Aug 2026 12:34:56 GMT"},
  {1798675200, "2026-12-31T00:00:00Z", 5, 365, "Thu, 31 Dec 2026 00:00:00 GMT"},
  {2085982096, "2036-02-07T07:28:16Z", 5, 38, "Thu, 07 Feb 2036 07:28:16 GMT"},
  -- the last second a 32-bit signed lua_Integer can hold
  {2147483647, "2038-01-19T03:14:07Z", 3, 19, "Tue, 19 Jan 2038 03:14:07 GMT"},
}

for _, c in ipairs(DATES) do
  local e, iso, wday, yday, http = c[1], c[2], c[3], c[4], c[5]
  local d = time.date(e)
  eq(time.format(e), iso, "date " .. iso)
  eq(d.wday, wday, iso .. " weekday")
  eq(d.yday, yday, iso .. " day of year")
  eq(time.format(e, time.HTTP), http, iso .. " as an HTTP date")
  eq(time.epoch(d), e, iso .. " round-trips back to its epoch")
end

local d = time.date(1785587696)
eq(d.year, 2026, "date() splits out the year")
eq(d.month, 8, "the month is 1-based")
eq(d.day, 1, "the day of the month")
eq(d.hour, 12, "the hour")
eq(d.min, 34, "the minute")
eq(d.sec, 56, "the second")
eq(time.date(nil), nil, "no epoch, no date")

-- Leap years come out of the March-based civil algorithm, not a table. The
-- 100-year rule is real but unreachable from here: 1900-02-28 is -2.2e9
-- seconds, outside the window a 32-bit signed epoch can hold at all
-- (1901-12-13 .. 2038-01-19). The 400-rule leap day is in range.
eq(time.format(time.epoch{year = 2000, month = 3, day = 1} - 86400),
   "2000-02-29T00:00:00Z", "2000 was a leap year (the 400-rule)")
eq(time.format(time.epoch{year = 2024, month = 3, day = 1} - 86400),
   "2024-02-29T00:00:00Z", "2024 was one (the 4-rule)")
eq(time.format(time.epoch{year = 2026, month = 3, day = 1} - 86400),
   "2026-02-28T00:00:00Z", "2026 is not")
eq(time.epoch{year = 2026, month = 13, day = 1},
   time.epoch{year = 2027, month = 1, day = 1},
   "month 13 normalises to January of the next year")

-- format() ---------------------------------------------------------------------

local E = 1767225600 -- 2026-01-01T00:00:00Z, a Thursday
eq(time.format(E), "2026-01-01T00:00:00Z", "the default format is ISO 8601")
eq(time.format(E, time.HUMAN), "2026-01-01 00:00:00", "the human format")
eq(time.format(E, "%y"), "26", "%y is the two-digit year")
eq(time.format(E, "%j"), "001", "%j is the zero-padded day of year")
eq(time.format(E, "%a %b"), "Thu Jan", "%a and %b are the English abbrevs")
eq(time.format(E, "%d/%m/%Y"), "01/01/2026", "a rearranged date")
eq(time.format(E, "100%%"), "100%", "%% is a literal percent")
eq(time.format(E, "%Q"), "%Q", "an unknown spec is left verbatim")
eq(time.format(E, "no specs"), "no specs", "a plain string is passed through")
eq(time.format(nil), nil, "formatting no epoch yields nothing, not a 1970 date")

-- the clock ---------------------------------------------------------------------

local T = 1785587696
time.reset()
eq(time.now(0), nil, "an unset clock has no time")
eq(time.valid(), false, "and says so")
eq(time.due(0), true, "an unset clock is always due a sync")
eq(time.format(time.now(0)), nil, "so printing it yields nil, not garbage")

eq(time.set(T, 1000), T, "set() takes the epoch and returns it")
eq(time.valid(), true, "the clock is valid once set")
eq(time.now(1000), T, "and reads back at the anchor tick")
eq(time.now(1999), T, "sub-second ticks do not advance it")
eq(time.now(2000), T + 1, "a whole second does")
eq(time.now(2999), T + 1, "and the remainder is kept, not dropped")
eq(time.tick, 2000, "the anchor advanced by the whole seconds only")
eq(time.now(3000), T + 2, "so the leftover 999 ms is not lost")
eq(time.now(2500), T + 2, "a tick that goes backwards holds the last value")
eq(time.now(1000 + 86400000), T + 86400, "a day of ticks is a day of clock")

eq(time.set("noon"), nil, "a non-number epoch is refused")
eq(select(2, time.set("noon")), "bad epoch", "and says why")
eq(time.set(1.5, 0), nil, "so is a fractional second")
eq(time.now(1000 + 86400000), T + 86400, "and a refused set changes nothing")

-- The device tick is uint32 ms pushed as a 32-bit *signed* integer: it turns
-- negative at ~24.8 days and wraps to 0 at ~49.7. Differences are exact mod
-- 2^32, so both rollovers pass through without a hiccup.
time.set(T, 0x7FFFFF00)
eq(time.now(0x7FFFFF00 + 100), T, "just before the signed-tick rollover")
eq(time.now(0x80000AF0), T + 3, "the tick going negative does not stop the clock")
eq(time.now(0x80001000), T + 4, "and it keeps counting on the negative side")

time.set(T, 0xFFFFF000)
eq(time.now(0xFFFFF000), T, "anchored just before the uint32 wrap")
eq(time.now(0x00000F00), T + 7, "and the wrap to 0 is spanned")

-- A gap wider than a signed difference can express (>24.8 days) reads as
-- negative; the clock holds rather than leaping backwards, and the next sync
-- repairs it.
time.set(T, 0)
eq(time.now(0x7FFFFFFF), T + 2147483, "24.8 days is still readable")
time.set(T, 0)
eq(time.now(-0x7FFFFFFF), T, "a longer gap stalls instead of going backwards")
eq(time.now(1000), T + 1, "and the stall left the anchor intact, not shifted")

-- resync policy
time.set(T, 5000)
eq(time.due(5000), false, "a just-set clock is not due")
eq(time.due(5000 + time.RESYNC - 1), false, "nor one ms before RESYNC")
eq(time.due(5000 + time.RESYNC), true, "but it is due at RESYNC")
eq(time.RESYNC, 3600000, "which is an hour")
eq(time.due(5000 - 0x7FFFFFFF), true, "a wrapped-past-readable gap is overdue")
eq(time.now(5000), T, "and due() never moves the clock itself")

-- the nab.time() fallback: no tick argument means ask the device
TICK = 5000
time.set(T)
TICK = 8000
eq(time.now(), T + 3, "now() with no tick reads nab.time()")
eq(time.due(), false, "and so does due()")
TICK = 8000 + time.RESYNC
eq(time.due(), true, "an hour of ticks later, a sync is due")

time.reset()
eq(time.now(0), nil, "reset() forgets the clock")
eq(time.due(0), true, "and it is due a sync again")

-- timezone ------------------------------------------------------------------
--
-- Instants below are the tzdb's own transition times for Europe/Berlin and
-- America/New_York.

time.zone(60, "EU") -- Berlin
eq(time.tz.offset, 60, "zone() stores the standard offset")
eq(time.tz.dst, "EU", "and the rule name")
eq(time.offset(1768478400), 60, "Berlin is UTC+1 in January")
eq(time.offset(1784116800), 120, "and UTC+2 in July")
eq(time.offset(1774745999), 60, "still CET one second before the change")
eq(time.offset(1774746000), 120, "CEST from 2026-03-29T01:00:00Z exactly")
eq(time.offset(1792889999), 120, "still CEST one second before the change back")
eq(time.offset(1792890000), 60, "CET again from 2026-10-25T01:00:00Z")
eq(time.offset(1806195600 - 1), 60, "2027 spring forward: before")
eq(time.offset(1806195600), 120, "2027 spring forward: at 01:00 UTC")
eq(time.offset(1824944400 - 1), 120, "2027 fall back: before")
eq(time.offset(1824944400), 60, "2027 fall back: at 01:00 UTC")

eq(time.format(time.localtime(1785587696), time.HUMAN), "2026-08-01 14:34:56",
   "local summer time in Berlin is UTC+2")
eq(time.format(time.localtime(1768478400), time.HUMAN), "2026-01-15 13:00:00",
   "and UTC+1 in winter")

time.zone(-300, "US") -- New York
eq(time.offset(1768478400), -300, "New York is UTC-5 in January")
eq(time.offset(1784116800), -240, "and UTC-4 in July")
eq(time.offset(1772953199), -300, "EST one second before the March change")
eq(time.offset(1772953200), -240, "EDT from 2026-03-08T07:00:00Z (02:00 local)")
eq(time.offset(1793512799), -240, "EDT one second before the November change")
eq(time.offset(1793512800), -300, "EST again from 2026-11-01T06:00:00Z")
eq(time.offset(1805007600 - 1), -300, "2027 spring forward: before")
eq(time.offset(1805007600), -240, "2027 spring forward: at 07:00 UTC")
eq(time.offset(1825567200 - 1), -240, "2027 fall back: before")
eq(time.offset(1825567200), -300, "2027 fall back: at 06:00 UTC")
eq(time.format(time.localtime(1785587696), time.HUMAN), "2026-08-01 08:34:56",
   "local summer time in New York is UTC-4")

-- the two rules really do differ: the EU changes three weeks after the US
time.zone(60, "EU")
eq(time.offset(1772953200), 60, "the EU has not switched on the US March date")
time.zone(-300, "US")
eq(time.offset(1774746000), -240, "and the US has already switched on the EU's")

time.zone(330) -- Delhi: a half-hour offset and no summer time
eq(time.offset(1768478400), 330, "a fixed zone stays put in January")
eq(time.offset(1784116800), 330, "and in July")
eq(time.format(time.localtime(1785587696), time.HUMAN), "2026-08-01 18:04:56",
   "a 30-minute offset lands on the half hour")

time.zone()
eq(time.offset(1784116800), 0, "zone() with no argument is UTC")
eq(time.localtime(1785587696), 1785587696, "so local time is the epoch itself")

time.reset()
eq(time.localtime(), nil, "localtime with no clock and no epoch is nil")
