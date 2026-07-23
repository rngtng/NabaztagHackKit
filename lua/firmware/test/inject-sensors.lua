-- Peripheral-injection golden probe (#42): read the button / RFID / ear
-- bindings through the real nab.* seam while the simulator injects those inputs
-- from a JSON-Lines timeline (test/inject-sensors.jsonl). The console output is
-- deterministic, so test/inject-sensors.expected pins it. Run standalone with:
--   task lua:apps:simulate APP=... INJECT=...   (see firmware:test:inject)
--
-- REPL note: each line is fed as its own #LC chunk, so state lives in globals
-- and each function body stays on one line (same as the other apps/*).
print("inject-sensors start")
print("button-default", nab.button())        -- no press injected yet -> false
print("rfid-default", nab.rfid())             -- no tag injected yet -> nil
nab.ear_move(1, 'forward')                    -- start ear 1; encoder advances
before = nab.ear_pos(1)
nab.delay(30)
print("ear-advanced", nab.ear_pos(1) > before) -- synthetic encoder moved -> true
nab.ear_stop(1)
print("button-injected", nab.button())        -- timeline held the button -> true
print("rfid-injected", nab.rfid())            -- timeline placed a tag -> its UID
print("inject-sensors done")
