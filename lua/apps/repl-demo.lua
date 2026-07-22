-- Sample Lua fed to the M4 firmwareV2 REPL in the simulator (#92):
--   task lua:apps:simulate APP=apps/repl-demo.lua
-- Each line is typed at the REPL prompt; output appears in the run summary's
-- `console output`. Stdlib is base + string + table (no math/io/os/coroutine).
-- NB: each line is its own chunk, so `local` does NOT persist across lines
-- (same as the stock `lua` interactive prompt); use globals to carry state.
print("hello from Lua " .. _VERSION)
for i = 1, 3 do print(i, string.rep("*", i)) end
t = { "a", "b", "c" }
print("#t =", #t, " t[2] =", t[2])
print("10//3, 10%3 =", 10 // 3, 10 % 3)   -- integer ops: exact
print(string.upper("it works"))
print(nope())                             -- runtime error -> REPL reports it and continues
