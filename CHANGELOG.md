# Changes

## Unreleased

  * fix [#81](https://github.com/rngtng/NabaztagHackKit/issues/81): add
    `task build:sim`. It wires the whole web-upload pipeline into one target —
    `build:boot` → `build:firmware` → sign (`mkfirmware:sign`) → verify
    (`mkfirmware:verify`) → `build/firmware/Nab.sim`. The `mkfirmware` tool is
    now `include`d in the root Taskfile, and the verify step confirms the
    signed `.sim` is well-formed (sentinels + declared length) so the produced
    file would be accepted by the config page.

## v2.0.0-alpha10 - 05-07-2026

  * fix [#37](https://github.com/rngtng/NabaztagHackKit/issues/37): `app-sse`
    now runs on hardware. It followed the simulator-only `lib/net/tcp.mtl` +
    `netstart` path unconditionally, but the firmware VM has no native TCP
    (`tcpListen`/`tcpSend`/… are simulator opcodes), so it silently did
    nothing on-device. `main.mtl` now picks its transport with the `SIMU`
    define like `app-template` — device builds compile the full `lib/net`
    stack (wifi/DHCP/DNS/IP/TCP) with `config_get_*` seams — and drives the
    broadcaster as a scheduler task so it coexists with the ipv4 TCP/DHCP/DNS
    tasks.
  * `task verify` now actually runs: the `build:app` steps had lost their
    `TARGET`, so the task aborted immediately. It chains
    piper + template + sse device builds, guarding the #37 regression.

## v2.0.0-alpha9 - 05-07-2026

  * `lib/chor/` — the choreography engine: the Violet chor-bytecode
    interpreter (LED/palette/ear/MIDI commands, waits, conditional jumps)
    and the classic color palettes, running on lib/hw + lib/audio; the app
    schedules `chor_run_cmd` through `chor_started_cb`/`chor_stopped_cb`
    seams. trame/streaming/interactive/info stay app-side (Violet server
    protocol). Tests: palette rows/modes, engine end-to-end on a crafted
    bytecode program. Suite 462 assertions / 40 scenarios.
    This completes the planned lib extraction: std, sys, net (full stack),
    hw, audio, chor, forth.

## v2.0.0-alpha8 - 05-07-2026

  * **the network stack lives in `lib/net`** — ipv4/ (ARP/IP/TCP/UDP/ICMP in
    MTL over raw 802.11 frames + the SIMU tcpudp_emu backend), wifi
    association, DHCP/DNS/NTP/HTTP clients, and the frame dispatcher; device
    configuration stays app-side behind `config_get_*` proto seams
  * one Sock implementation: the ipv4 assembly exports writetcp/closetcp/
    tcpcb/listentcp aliases, so the lib servers run on the stack; app-piper's
    net/sock.mtl copy is gone. lib/net/http.mtl closes the audiolib
    http-client seam; ntp closes the lib/sys/time seam
  * `src/app-template` is device-capable: SIMU builds use the bare tcp.mtl
    transport, device builds compile the full stack (461 funs) configured by
    an "edit these" block in app.mtl
  * lib/net modules are self-contained (own std/sys/hw includes); TCP event
    consts canonical in lib/protos/tcp_protos.mtl
  * tests: http client header machinery, DNS server-list handling, NTP
    payload; suite 451 assertions / 38 scenarios
  * CLAUDE.md gotchas: duplicate-definition binding rule (call sites bind the
    most recent definition at their point of compilation), shared types
    across redefinitions

## v2.0.0-alpha7 - 04-07-2026

  * `lib/audio/` — `audiolib` (WAV playback engine: local sample lists or
    HTTP streaming with flow-controlled buffering, button3 volume curve)
    and `midi` (embedded notes/jingles); app keeps const_data assets and
    the record-upload flow
  * `lib/protos/http_client_protos.mtl` — the HTTP-client contract
    lib/audio streams through (implemented by app net/http.mtl until the
    lib/net client extraction)
  * tests: `test/lib/audio/` (volume curve, silence padding, idle-state
    predicates, MIDI table well-formedness); suite 435 assertions /
    35 scenarios

## v2.0.0-alpha6 - 04-07-2026

  * `lib/hw/` — rabbit hardware building blocks on VM natives:
    `button` (click/double/long-click events), `leds` (setters, override
    table, breathing oscillator), `ears` (motor state machine with
    `ears_touched_cb` / `ears_post_run_cb` app seams), `rfid`
    (`rfid_poll` debounced tag detection), `reclib` (microphone → WAV)
  * app-piper keeps only policy: LED animations, RFID reaction
    (jingle/chor/forth hook), record-upload flow, interactive ear hooks
  * new `lib/protos/{button,leds,colors,ears,reclib}_protos.mtl`; app
    protos are thin shims (plus app-only bits like the MASK_* consts)
  * tests: `test/lib/hw/` — oscillator/scaling, ears position arithmetic
    and go-target math, button events, RFID id parsing, WAV container
    layout; suite now 423 assertions / 33 scenarios

## v2.0.0-alpha5 - 04-07-2026

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

## v2.0.0-alpha4 - 04-07-2026

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

## v2.0.0-alpha - 2026-06-30

Complete SDK reboot. Ruby gem gutted; repo is now a layered MTL SDK + toolchain.

  * Dockerized MTL toolchain (`tools/mtl_linux`) — compile and simulate `.mtl` on any host
  * Dockerized C preprocessor (`tools/preprocessor`) — `#include`/`#ifdef` for MTL, no host deps
  * Dockerized firmware packager (`tools/mkfirmware`) — `.bin` → signed `.sim` for OTA upload
  * Root `Taskfile.yaml` wires all layers: `task build:boot`, `task build:firmware`, `task test`, `task simulate:boot`
  * Reusable MTL standard library (`lib/`) — string, list, integer, base64, URL, JSON, MD5, HTTP, SSE, Forth interpreter
  * MTL test framework (`test/`) — assertion-based unit tests run in the simulator
  * Boot image (`src/boot/`) — WiFi provisioning + OTA firmware upgrade, simulated via `task simulate:boot`
  * C firmware layer (`src/firmware/`) — ARM7TDMI VM via nabgcc/GCC toolchain
  * Application layer (`src/app-piper/`) — Forth interpreter + hardware words (audio, RFID, LEDs, ears, networking)
  * JTAG debrick guide + Raspberry Pi configs (`tools/openocd/`)
  * `NABAZTAG_SDK.md` — architecture rationale and TODOs
  * `PROVENANCE.md` — vendored-source origins

## v2.0.0-alpha3 - 23-06-2026

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
