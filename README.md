# Nabaztag Hack Kit

Everything you need to hack the Rabbit: a dockerized MTL toolchain to compile and run custom bytecode on Nabaztag v1/v2, plus the original compiler sources for linux and a modified mac os x version.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Getting Started

The only host requirements are [Docker](https://www.docker.com/) and [Task](https://taskfile.dev). Every build runs inside Docker — no toolchain to install locally.

Run `task` with no args to list all targets:

```
task
```

### Compile & Simulate

Compile an MTL source to bytecode:

```
task mtl:compile SOURCE=bytecode/main.mtl
```

Writes `<name>.bin` (signed device-flash format) into the current dir. Pass `OUT=<dir>` to change the output dir, or `SIGN=false` for raw bytecode.

Run an app in the simulator:

```
task mtl:simulate SOURCE=bytecode/main.mtl
```

### Layout

  * `bytecode/` — MTL sources: `main.mtl`, the `lib/` API (see **API** below), and the original violet sources under `_original/`
  * `tools/mtl_linux/` — dockerized linux compiler (`mtl_compiler`) and simulator (`mtl_simu`)
  * `ext/bin/` — prebuilt mac os x binaries (`mtl_comp`, `mtl_simu`, `mtl_merge`) by @ztalbot2000
  * `src/firmware/`, `src/boot/` — C firmware and boot sources
  * `docs/` — grammar, commands and hardware notes

The linux sources are (more or less) the original ones by violet; the mac os x version was created by @ztalbot2000.

## Understanding the Bytecode

The bytecode is written in a custom language by Sylvain Huet. It is referenced as _Metal_ and files end with `.mtl`. Unfortunately documentation is very poor (and in french). Check directory `bytecode/_original` which contains a basic overview & documentation as well as a list of (common) commands. A good reference is the original bytecode, included in the directory as well. Major parts got extracted into seperate files, found in `bytecode/lib/` directory and ready to be included in your code.

Grammar: https://docs.google.com/document/d/1KMg2wSyMKTmsilCpOByi_59uk5dD8XMfGAu20W63kZE/edit?hl=en_US

### Testing

The kit includes a simple test framework to test custom bytecode. See `bytecode/test/test.mtl`. A typical test looks like this:

```c
 let test "math operations" -> t in
  (
    //assertions
    assert_equalI 0 10 - (2 * 5);
  0);
```

The framework offers assertions similar to [Ruby Test::Unit](http://ruby-doc.org/stdlib-1.9.3/libdoc/test/unit/rdoc/Test/Unit.html) style. Mind that the variable type has to be given
explicit. Convention is:

  * I = integer
  * S = string
  * L = list
  * T = table

Following assertions are available (see bytecode/test/helper.mtl)

  * assert_equalI I I
  * assert_equalI S S
  * assert_nil I
  * assert_equalIL
  * assert_equalSL
  * assert_equalTL

## API
As example and for my own purposes I implemented a simple API to deal with RFID, LEDS, BUTTON and EARS easily. (see bytecode/main.mtl)

### Input Devices

#### RFID
see my other project *NabaztagInjector*


#### BUTTON
Current Button has very basic functionality: a short press send HTTP Request of type `Log` to server, a long
press forces the bunny to restart.

### Output Devices
Data for all output devices are stored in buffers. Each device has two: one for onetime, imediate playback, another for permanent loops.

#### LEDS
Buffers 0 - 9, where 0-4 are used for onetime, and 5-9 for loop playback.

#### EARS
Buffers 10 - 13, where 10 & 11 are used for onetime, and 12 & 13 for loop playback.


## Disclamer

Compiler code copied from [OpenJabNab](https://github.com/OpenJabNab/OpenJabNab). Thanks!


## Nabaztag Background
Read following posting for more background on Nabaztag Hacking (uses google translate:)

  * [First class info from the creator himself](http://www.sylvain-huet.com/?lang=en#nabv2)
  * [A good nabaztag Blog](https://www.journaldulapin.com/tag/nabaztag/)
  * [A good introduction to understand Nabaztag Protocol](http://www.sis.uta.fi/~spi/jnabserver/documentation/index.html)
  * [Les source bytecode et compilateur](http://translate.googleusercontent.com/translate_c?hl=en&rurl=translate.google.com&sl=fr&tl=en&twu=1&u=http://nabaztag.forumactif.fr/t13241p30-les-sources-bytecode-et-compilateur&usg=ALkJrhjLTbx1GMfSUgwhdjES1LzlE07HZQ#338060)
  * [Mindscape donne une seconde vie a nabaztag](http://translate.google.com/translate?hl=en&sl=fr&tl=en&u=http%3A%2F%2Fwww.planete-domotique.com%2Fblog%2F2011%2F08%2F07%2Fmindscape-donne-une-seconde-vie-a-nabaztag%2F)

## Related Projects

New firmware with WPA2
1. https://github.com/ccarlo64/firmware_nabaztag
(see http://nabaztag.forumactif.fr/t15323-firmware-for-wpa-wpa2-test)

2. https://github.com/RedoXyde/nabgcc
  https://github.com/RedoXyde/mtl_linux
  (see http://nabaztag.forumactif.fr/t15280p25-nabaztagtag-en-wpa2)

ServerlessNabaztag
-> https://github.com/andreax79/ServerlessNabaztag

CloudServer replace: http://nabaztaglives.com/

Rebuild Nabaztag:
- https://www.hackster.io/bastiaan-slee/nabaztag-gets-a-new-life-with-google-aiy-e9f2c8


## Websocket

Websocket HowTo:
- https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers

- http://blog.honeybadger.io/building-a-simple-websockets-server-from-scratch-in-ruby/
- https://github.com/pusher/websockets-from-scratch-tutorial


## Future
I'd like to hack the Violet mir:ror too. Some starting points:

  * https://github.com/leh/ruby-mirror
  * http://reflektor.sourceforge.net/
  * http://www.instructables.com/id/Kids-check-in-and-check-out-with-hacked-Mirror-an/
  * http://svay.com/blog/hacking-rfid-with-nodejs/
  * http://arduino-projects4u.com/violet-mirror/
