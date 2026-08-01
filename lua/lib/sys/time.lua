-- sys.time - the wall clock (#259): an NTP-set epoch anchored to the device
-- tick, civil date conversion, strftime-subset formatting, and a timezone
-- offset with a DST rule.
--
-- `nab.time()` alone is milliseconds since boot and nothing more, so nothing
-- on the rabbit could name a date. This turns one NTP reading (sys.ntp, via
-- net.iface's :ntp) into a clock that keeps running between syncs, on top of
-- a tick that is neither wall time nor even monotonic for long.
--
-- Everything is integer arithmetic on Unix seconds. `os.date`/`os.time` are
-- not an option - the device registers base + table + string + nab only.
--
-- Device stdlib is base + table + string only; this file is one luac chunk
-- that extends the global `sys` table (there is no require on the rabbit).

sys = sys or {}
local time = {}
sys.time = time

-- clock state (public: the REPL and the DoD flows read these directly)
time.base = nil   -- Unix seconds at .tick
time.tick = nil   -- the nab.time() reading .base is anchored to
time.synced = nil -- .tick of the last set(); what due() ages

-- Milliseconds between NTP syncs. The 1 ms tick comes off the CPU clock, not
-- a crystal-trimmed RTC, so it drifts; an hour is what mtl's ntp.mtl used.
time.RESYNC = 3600000

-- civil calendar --------------------------------------------------------------
--
-- Howard Hinnant's days<->civil pair (public domain), proleptic Gregorian,
-- shifted to a March-based year so the leap day lands last and no month table
-- is needed. Lua's // is floor division, which is what these want, so the
-- negative-era correction C++ needs is not repeated here. Every intermediate
-- stays well inside a 32-bit signed lua_Integer for any year the 2038 epoch
-- limit can reach.

local function civil(days) -- days since 1970-01-01 -> year, month, day
  local z = days + 719468
  local era = z // 146097
  local doe = z - era * 146097                                    -- [0,146096]
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)           -- [0,365]
  local mp = (5 * doy + 2) // 153                                 -- [0,11]
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp < 10 and mp + 3 or mp - 9
  return yoe + era * 400 + (m <= 2 and 1 or 0), m, d
end

local function days_from(y, m, d) -- year, month, day -> days since 1970-01-01
  y = m <= 2 and y - 1 or y
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m > 2 and m - 3 or m + 9) + 2) // 5 + d - 1
  return era * 146097 + yoe * 365 + yoe // 4 - yoe // 100 + doy - 719468
end

-- Unix seconds -> {year,month,day,hour,min,sec,wday,yday}. wday is 1..7 with
-- 1 = Sunday (os.date's convention); 1970-01-01 was a Thursday. Negative
-- epochs work - floor division puts pre-1970 seconds on the right day.
function time.date(epoch)
  if not epoch then return nil end
  local days = epoch // 86400
  local sod = epoch % 86400 -- floor mod: always 0..86399, even for epoch < 0
  local y, m, d = civil(days)
  return {year = y, month = m, day = d,
          hour = sod // 3600, min = sod // 60 % 60, sec = sod % 60,
          wday = (days + 4) % 7 + 1,
          yday = days - days_from(y, 1, 1) + 1}
end

-- {year,month,day[,hour,min,sec]} -> Unix seconds. The inverse of date(); it
-- normalises out-of-range fields (month 13 is January of the next year), which
-- is what the DST rules below lean on.
function time.epoch(t)
  return days_from(t.year, t.month, t.day) * 86400
         + (t.hour or 0) * 3600 + (t.min or 0) * 60 + (t.sec or 0)
end

-- formatting ------------------------------------------------------------------

local WDAY = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}
local MONTH = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}

-- Two lookups and one branch rather than a closure per spec: eleven small
-- functions cost about 1.5 KB of bytecode, and none of this is a hot path.
local FIELD = {Y = "year", y = "year", m = "month", d = "day",
               H = "hour", M = "min", S = "sec", j = "yday"}
local PAD = {Y = "%04d", j = "%03d"} -- everything else is two digits

time.ISO = "%Y-%m-%dT%H:%M:%SZ"       -- ISO 8601 (fmt default)
time.HTTP = "%a, %d %b %Y %H:%M:%S GMT" -- RFC 1123, for Date/If-Modified-Since
time.HUMAN = "%Y-%m-%d %H:%M:%S"

-- Unix seconds -> string. strftime subset: %Y %y %m %d %H %M %S %j %a %b %%;
-- an unknown spec is left verbatim. -> string | nil (epoch nil, i.e. no clock)
function time.format(epoch, fmt)
  if not epoch then return nil end
  local t = time.date(epoch)
  return ((fmt or time.ISO):gsub("%%(.)", function(c)
    local f = FIELD[c]
    if f then
      return (PAD[c] or "%02d"):format(c == "y" and t[f] % 100 or t[f])
    end
    if c == "a" then return WDAY[t.wday] end
    if c == "b" then return MONTH[t.month] end
    if c == "%" then return "%" end
    return "%" .. c
  end))
end

-- the clock -------------------------------------------------------------------
--
-- The device tick is `nab.time()`: uint32 milliseconds since boot pushed as a
-- 32-bit *signed* lua_Integer, so it turns negative after ~24.8 days and wraps
-- to 0 after ~49.7. Differences are taken in that same wrap arithmetic, where
-- `tick - anchor` is exact modulo 2^32, and the anchor is advanced on every
-- read - so the clock rides both rollovers indefinitely as long as now() is
-- called at least once per 24.8 days, the widest gap a signed difference can
-- represent. A longer gap reads as negative and the clock holds its last value
-- instead of jumping backwards; the next sync repairs it.

local function tick_now(tick)
  if tick then return tick end
  return nab.time()
end

-- Set the wall clock from an NTP (or any) Unix-seconds reading, anchored to
-- `tick` (default: now). -> epoch | nil, err
function time.set(epoch, tick)
  if type(epoch) ~= "number" or epoch ~= epoch // 1 then
    return nil, "bad epoch"
  end
  tick = tick_now(tick)
  -- `| 0` turns a whole-valued float into a real integer, so every later
  -- addition stays integer arithmetic
  time.base, time.tick, time.synced = epoch | 0, tick, tick
  return time.base
end

-- Current Unix seconds, or nil while the clock has never been set.
function time.now(tick)
  local base = time.base
  if not base then return nil end
  local d = tick_now(tick) - time.tick
  if d >= 1000 then
    local secs = d // 1000
    base = base + secs
    time.base, time.tick = base, time.tick + secs * 1000
  end
  return base
end

function time.valid() return time.base ~= nil end

-- true when the clock wants an NTP sync: never set, or RESYNC ms since the
-- last one. Pull-style like the rest of the stack - the app polls this and
-- calls ifc:ntp() itself, so nothing here owns a timer or a socket.
function time.due(tick)
  if not time.base then return true end
  local d = tick_now(tick) - time.synced
  return d < 0 or d >= time.RESYNC -- negative: >24.8 d unsynced, so overdue
end

-- Forget the clock (mainly for tests and a re-provision).
function time.reset() time.base, time.tick, time.synced = nil, nil, nil end

-- timezone --------------------------------------------------------------------
--
-- A base offset plus an optional DST rule. mtl ships a 114-entry city table
-- (sys/timezones.mtl) and still cannot say whether summer time is running; two
-- rules cover everywhere a rabbit is plausibly plugged in, and the base offset
-- is a single config number. Southern-hemisphere zones are not modelled - set
-- the offset and leave the rule off.

time.tz = {offset = 0, dst = nil} -- minutes east of UTC (standard time), rule
time.DST_MINUTES = 60

-- Unix seconds of 00:00 UTC on the n-th Sunday of a month; n < 0 counts back
-- from the end (-1 = the last Sunday).
local function sunday(year, month, n)
  local d = n > 0 and days_from(year, month, 1) or days_from(year, month + 1, 0)
  local w = (d + 4) % 7 -- 0 = Sunday
  if n > 0 then return (d + (7 - w) % 7 + (n - 1) * 7) * 86400 end
  return (d - w + (n + 1) * 7) * 86400
end

time.DST = {}

-- EU: last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC. The
-- whole union switches on the same UTC instant, so the offset is irrelevant.
function time.DST.EU(epoch)
  local y = civil(epoch // 86400)
  return epoch >= sunday(y, 3, -1) + 3600 and epoch < sunday(y, 10, -1) + 3600
end

-- US: second Sunday of March to first Sunday of November, both at 02:00 local
-- *standard* time - and since November's 02:00 is quoted in daylight time, it
-- is 01:00 standard. Compared in standard time, hence the offset argument.
function time.DST.US(epoch, offset)
  local s = epoch + offset * 60
  local y = civil(s // 86400)
  return s >= sunday(y, 3, 2) + 2 * 3600 and s < sunday(y, 11, 1) + 3600
end

-- Set the zone: offset in minutes east of UTC (standard time), rule "EU"/"US"
-- or nil for none. time.zone(60, "EU") is Berlin, time.zone(-300, "US") is
-- New York.
function time.zone(offset, dst)
  time.tz = {offset = offset or 0, dst = dst}
  return time.tz
end

-- Total offset in minutes east of UTC at that instant, DST included.
function time.offset(epoch)
  local off = time.tz.offset or 0
  local rule = time.tz.dst and time.DST[time.tz.dst]
  if rule and epoch and rule(epoch, off) then off = off + time.DST_MINUTES end
  return off
end

-- `epoch` (default: now()) shifted into local time - feed it to date()/format().
-- The result is not a Unix timestamp any more, it is a local-clock reading.
-- -> seconds | nil (no clock set)
function time.localtime(epoch)
  epoch = epoch or time.now()
  if not epoch then return nil end
  return epoch + time.offset(epoch) * 60
end
