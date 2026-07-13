-- Round-trip fixture for the off-device luac pipe (#133).
--
-- Fed to the REPL two ways - as source, and as #LC bytecode frames produced by
-- tools/luac/replpipe.py - it must produce byte-identical console output (see
-- `task test:firmwareV2:luac`). Keep it print-only and error-free: an error
-- would name its chunk ("=stdin" for a frame vs. [string "..."] for a parsed
-- line), and the differing chunknames would break that identity. Each line also
-- stands alone - the REPL compiles one chunk per line, so a `local` would not
-- survive to the next line; cross-line state uses globals.
--
-- Mixes bare expressions (which the REPL echoes via print) with statements, to
-- exercise both the "return <line>" and verbatim compile paths.
print("luac round-trip")
1 + 2
"nabaztag" .. "-" .. "tag"
string.format("%d/%s", 42, "ok")
print(#"hello", 10 % 3)
t = {10, 20, 30}
print(t[1] + t[2] + t[3])
for i = 1, 3 do print("i=" .. i) end
