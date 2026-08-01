-- Acceptance demo for #283: can the four workloads compose?
--
-- Runs ONE scenario - a choreography move (ear to a target) running while the
-- script does a stretch of blocking work, with button edges arriving
-- throughout - and grades the runtime on three criteria. It FAILS all three
-- today and must PASS all three once the cooperative scheduler lands.
--
-- Standalone (both lines needed - see the one-chunk note below):
--   task lua:apps:compile APP=firmware/test/sched-demo.lua OUT=firmware/test/sched-demo.lc
--   task lua:apps:simulate APP=firmware/test/sched-demo.lc INJECT=firmware/test/sched-demo.jsonl
-- As the golden: task lua:firmware:test:sched
--
-- ONE CHUNK, NOT PER-LINE. replpipe.py frames a *.lua app one chunk per source
-- line, and the REPL runs its idle event pump *between* lines - which would
-- deliver the very events this demo proves are starved, and grade a runtime
-- that isn't the one under test. So the harness compiles to .lc first and feeds
-- the whole demo as a single chunk, exactly like a real resident app. That is
-- also why multi-line function bodies are fine here (contrast
-- test/inject-sensors.lua, which is fed per-line and must keep bodies on one).
--
-- DETERMINISM - why every gate is a console marker, never a timestamp. The
-- simulator's injection clock (`elapsed_ms`, one per instruction slice) is NOT
-- the firmware's `counter_timer`: elapsed_ms starts at reset, while
-- counter_timer only starts once init_tick() enables the timer IRQ, ~1500
-- slices later, and thereafter advances only when interrupts are unmasked. So
-- an `at_ms` gate and a nab.time() reading are ~1.5 s apart and drift besides.
-- Instead the demo prints a marker at each point where an input is due and
-- records nab.time() there; sched-demo.jsonl gates on those markers (`after`).
-- The scenario is then anchored to the firmware's own progress - exact, and
-- stable as the ELF gets faster or slower underneath it.
--
-- Recording nab.time() at each marker also makes latency exact: the injection
-- lands on the slice that observes the marker, so mark_t[i] IS the injection
-- time on the same clock the callback timestamps with.
--
-- WHY NO RFID CRITERION. The coupler scan cycle is 750 ms (event.c
-- RFID_PERIOD_MS), which dominates a ~1 s window: a tag injected mid-work is
-- first scanned at about the same moment whether or not the runtime pumps
-- during blocking work, so it would discriminate nothing. The button (20 ms
-- debounce) and the ear encoder (8 counts/ms in the sim) are the signals that
-- actually separate a starved runtime from a pumping one.
--
-- OUTPUT IS VERDICTS ONLY - never raw timings or counts, which drift with the
-- ELF. Set VERBOSE=true before running for the measured numbers.

print("sched-demo start")

-- ---- scenario constants ----------------------------------------------------
WORK_A     = 150   -- blocking work between marks 1-2 and 2-3
WORK_B     = 300   -- blocking work after mark 3, to the end of the window
SETTLE     = 150   -- pumped wait, twice: before and after mark 4
EAR_TARGET = 400   -- encoder counts after which the ear must stop
EAR_TOL    = 120   -- counts of slop allowed (sim encoder runs 8 counts/ms)
LAT_MAX    = 50    -- ms: worst acceptable event latency (20 ms is debounce)
EDGES_WANT = 4     -- press A down/up (transient) + press B down/up

-- Total blocking work is WORK_A + WORK_A + WORK_B = 600 ms. Splitting it into
-- three calls is faithful, not a dodge: a real app blocks in a sequence of
-- calls (play a clip, then fetch a URL), and nothing pumps *between* them
-- either - they are all one chunk, and the REPL's idle pump only runs between
-- input lines. The splits exist so the timeline has marks to gate on.

-- ---- observation -----------------------------------------------------------
edges = {}    -- observed button edges: {down=bool, t=ms}
mark_t = {}   -- mark_t[i] = nab.time() when mark i printed = injection time

nab.on("button", function(down)
  edges[#edges + 1] = {down = down, t = nab.time()}
end)

function mark(i)
  print("mark-" .. i)
  mark_t[i] = nab.time()
end

-- ---- choreography: drive ear 1 and stop it EAR_TARGET counts on -------------
-- This is the ears.lua :step() contract in miniature: the motor runs free once
-- nab.ear_move starts it, so *something* has to keep checking the encoder and
-- call the stop. Nothing does that during a blocking call today.
ear0 = nab.ear_pos(1)
ear_stopped_at = nil

function ear_delta()
  return (nab.ear_pos(1) - ear0) & 0xFFFF
end

function earwatch()
  if ear_stopped_at then return end
  local d = ear_delta()
  if d >= EAR_TARGET then
    nab.ear_stop(1)
    ear_stopped_at = d
  end
end

-- Hand the watcher to the reactor if there is one. Today `sched` is nil, so
-- nothing pumps it and the ear sails past its target; after #283 the scheduler
-- pumps it throughout the blocking work. The demo file itself is unchanged
-- between the red and green runs - only the runtime beneath it differs.
if sched then sched.pump(earwatch) end

-- ---- run the scenario ------------------------------------------------------
nab.ear_move(1, "forward")

mark(1)              -- press A down
nab.delay(WORK_A)
mark(2)              -- press A up: A is now entirely inside blocking work
nab.delay(WORK_A)
mark(3)              -- press B down, held past the end of the work window
nab.delay(WORK_B)

earwatch()                             -- last chance to stop on target
if not ear_stopped_at then
  nab.ear_stop(1)
  ear_stopped_at = ear_delta()
end

nab.wait(SETTLE)     -- pumped: a starved runtime reports B here, late
mark(4)              -- press B up
nab.wait(SETTLE)

-- ---- grade -----------------------------------------------------------------
-- Latency pairs each observed edge with its injection time POSITIONALLY, which
-- is only meaningful once every edge arrived - with edges missing, edges[i] is
-- not the edge mark_t[i] injected and the difference is noise. So latency is
-- computed only when C1 holds; otherwise it is not measured at all (-1) rather
-- than reported as a misleading number.
c1 = #edges >= EDGES_WANT

worst = -1
if c1 then
  worst = 0
  for i = 1, #edges do
    local late = edges[i].t - mark_t[i]
    if late > worst then worst = late end
  end
end

overshoot = ear_stopped_at - EAR_TARGET
if overshoot < 0 then overshoot = -overshoot end

c2 = c1 and worst <= LAT_MAX
c3 = overshoot <= EAR_TOL

function verdict(ok)
  if ok then return "PASS" else return "FAIL" end
end

if VERBOSE then
  print("edges", #edges, "worst-latency-ms", worst, "ear-overshoot", overshoot)
end

print("C1 edges-during-blocking-work " .. verdict(c1))
print("C2 event-latency " .. verdict(c2))
print("C3 ear-stops-on-target " .. verdict(c3))

score = 0
if c1 then score = score + 1 end
if c2 then score = score + 1 end
if c3 then score = score + 1 end
print("<<SCHED-DEMO " .. score .. "/3>>")
