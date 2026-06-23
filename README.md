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

Compile an MTL boot source to bytecode:

```
task build:boot
```

Run the boot app in the simulator:

```
task simulate:boot
```

Finally, compile the firmware sources to bytecode,including the boot code:

```
task build:firmware
```

### Layout

  * `bytecode/` — MTL sources: `main.mtl`, the `lib/` API (see **API** below), and the original violet sources under `_original/`
  * `tools/mtl_linux/` — dockerized linux compiler (`mtl_compiler`) and simulator (`mtl_simu`)
  * `ext/bin/` — prebuilt mac os x binaries (`mtl_comp`, `mtl_simu`, `mtl_merge`) by @ztalbot2000
  * `src/firmware/`, `src/boot/` — C firmware and boot sources
  * `docs/` — grammar, commands and hardware notes

The linux sources are (more or less) the original ones by violet; the mac os x version was created by @ztalbot2000.

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

- http://petertyser.com/nabaztag-nabaztagtag-dissection/

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



## Compilation

In order to compile the code, make use of https://github.com/rngtng/mtl_linux (aktive fork of https://github.com/RedoXyde/mtl_linux)

### Mac OSX / macOS

Simplest way on macOS is using [homebrew](https://brew.sh/). Find the latest formular here: https://github.com/rngtng/homebrew-mtl_linux with

```
brew tap rngtng/homebrew-mtl_linux
brew install mtl_linux
```

## Test

Once compiler & simulator installed, run the test

```
make test
```

## About mtl (Metal) Vlisp

MTL/Metal is a custom language by [Sylvain Huet](http://www.sylvain-huet.com/?lang=en). It is referenced as _Metal_ and files end with `.mtl`. Unfortunately documentation is very poor (and in french). Find the Grammar rules here: https://docs.google.com/document/d/1KMg2wSyMKTmsilCpOByi_59uk5dD8XMfGAu20W63kZE/edit?hl=en_US

Metal is used for programming the Nabaztag, where with `bytecode` the program name is referenced. See original sources here: `/_sources/original/`.


