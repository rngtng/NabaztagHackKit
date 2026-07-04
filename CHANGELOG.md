# Changes

## v0.5.0 - 04-07-2026

  * `src/app-template/` — blueprint proving "new app = lib blocks + business
    logic": composes the TCP adapter, HTTP server and Forth core; `app.mtl`
    is a Forth playground (`POST /eval` runs code, answers output + stack);
    run with the new `task simulate:app` (port 8080)
  * `lib/net/tcp.mtl` — VM-native TCP adapter (tcpSend/tcpListen/tcpCb →
    writetcp/listentcp API) extracted from the SSE test app
  * `lib/sys/time.mtl` + `timezones.mtl` moved from app-piper with config/NTP
    proto seams; **fixed two upstream date bugs**: the day calculation could
    report the previous day (truncated low word), and HTTP date parsing never
    matched month names (case mismatch)
  * app-piper `utils/utils.mtl` deduplicated: 44 of 55 funs were lib/std
    copies; itoanil/liststrlen/mac_to_hex/dump helpers landed in lib
  * `task test` now also fails on compiler errors reported via stderr
  * suite: 379 assertions / 28 scenarios

## v0.4.0 - 04-07-2026

  * `lib/` finalized as the canonical reusable MTL library: `std/` (pure data),
    `sys/` (task scheduler, firmware/bytecode/system/echo), `net/` (sock,
    http_server, sse_server on VM-native TCP), `forth/` (interpreter core with
    word helpers, core dictionary, compile words, JSON parser)
  * removed 22 stale flat `lib/*.mtl` files (outdated duplicates + unused
    Violet-era helpers)
  * `app-piper` now consumes lib instead of bundling copies: b64, url, md5,
    json, word, xmlparser, task scheduler, and the entire Forth core
    (9 duplicated files + 92 dictionary entries removed); telnet REPL rewired
    to the lib `write_fn`/`readline_cb` I/O abstraction
  * test suite: 358 assertions / 27 scenarios (`task test`) — new md5, sock,
    http_server, task, xmlparser, forth (stack/arithmetic/comparison/logical/
    control/string/list/output/json) and fixarg-semantics suites;
    test tree mirrors lib layout
  * fixed upstream forth bugs found by the tests: ROT rotated backwards,
    TUCK behaved like OVER, `*/` and `*/mod` divided by the wrong operand,
    JSON true/false/null in containers looped until out-of-memory,
    `write_fn` output path never compiled
  * new `task build:app` compiles app bytecode (preprocess + mtl compile)

## v0.3.0 - 23-06-2026

  * added [dockerized Linux mtl](https://github.com/rngtng/mtl_linux) compiler and simulator under `tools/mtl_linux`
  * Taskfile tasks to build and run the compiler/simulator via Docker

## v0.2.1 - 18-09-2016

  * pulled in mac os x support by @ztalbot2000 28-06-2014
    * fixed all compile warnings
    * Added -m32 compile option
    * fixed segfault when running mtl_comp with invalid args
    * Changed output so size is CAPITAL hex value to make absolutely no
      differences when comparing compiled .mtl file and original bc.jsp file

## v0.1.1 - 17-09-2016

  * file cleanup fix spelling
  * update readme on how to use/run examples
  * proper usage of bundler

## v0.1.0 - xx-09-2012

  * files restructured, separation byte & ruby code
  * update server for generic structure, added callback
  * mtl binaries respect defaults from local `.mtlrc` file

## v0.0.2 - 29-01-2012

  * rename to NabaztagHackKit
  * moved rake task to binaries
  * fixed tests

## v0.0.1 - 28-01-2012

  * inital release
