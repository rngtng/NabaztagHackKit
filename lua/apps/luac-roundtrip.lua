-- Golden-transcript fixture for the bytecode pipeline (#128).
--
-- Compiled off-device to #LC frames (tools/luac/replpipe.py) and run on the
-- parser-less sim, this must reproduce luac-roundtrip.expected byte-for-byte
-- (`task lua:firmware:test:luac`). Regenerate the golden after editing:
--   task lua:firmware:test:luac REGEN=1
--
-- Keep it print-only and error-free. The REPL compiles one chunk per line, so a
-- `local` would not survive to the next line - cross-line state uses globals.
-- Comment/blank lines compile to empty chunks (each echoes a bare "> " prompt,
-- proving they run cleanly). Mixes bare expressions (echoed via print) with
-- statements, exercising replpipe's "return <line>" and verbatim compile paths.
print("luac round-trip")
1 + 2
"nabaztag" .. "-" .. "tag"
string.format("%d/%s", 42, "ok")
print(#"hello", 10 % 3)
t = {10, 20, 30}
print(t[1] + t[2] + t[3])
-- float printing (#222): float -> integer part + ".0" (approximate, no dtoa);
-- integers stay bare. Guards the non-variadic luai_num2str path from regressing.
3.14
print(2^20, 1/2)
print(10 // 3, 3 == 3.0)
for i = 1, 3 do print("i=" .. i) end
