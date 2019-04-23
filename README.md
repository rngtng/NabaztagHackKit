# Nabaztag Hack Kit

Everything you need to hack the Rabbit: a sinatra server including simple api framework to run custom bytecode on Nabaztag v1/v2. Includes original compiler sources for linux and a modified mac os x version.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Getting Started

### Installation

The Hack Kit is distributed as a ruby gem. It comes with a simple web server (based on sinatra) which runs out-of-the for connecting you rabbit and distributing the nabaztag bytecode. In addition it includes sinatra helpers/modules to communicate with the rabbit easily. Lastly it provides binaries to compile your own Nabaztag bytecode (see **Binaries** below).

### Simple Server

The Server is the communication endpoint for the rabbit. Its two main purposes are:

  1. serving the bytecode on bootup
  2. receive and respond to HTTP requests in a defined format.

### Setup

1. Install dependencies first:

```
gem install nabaztag_hack_kit
```

or if you have a `Gemfile`

```
bundle install --path=vendor/bundle
```

2. Then, create a `config.ru` file

```ruby
require 'nabaztag_hack_kit/server'

run NabaztagHackKit::Server.new
```

3. Finally, to start and run the server, execute:

```
bundle exec rackup -p <portnumer>
```

See `examples/` folder for more sophisticated usage.

### Binaries

The kit comes with violet sources and binaries to compile custom Nabaztag bytecode. See folder `compiler/`. The linux sources are (more or less) the original ones by violet, the mac osx version was created by @ztalbot2000.
The compiler binaries are compiled on installation of the gem.

Following three binaries are available:

#### mtl_comp

A wrapper around `mtl_comp`. Compiles a `*.mtl` file. It calls `mtl_merge` before

#### mtl_simu

A wrapper around `mtl_simu`.  Runs a `*.mtl` file. It calls `mtl_merge` before

#### mtl_merge

Merges multiple `*.mtl` files into one. Files are included like in C: `#include "<relative path to file>"`. Output is temporary file `.tmp.mtl`.

## Development

Be sure to checkout `mtl_linux` submodule first:

```
git submodule update
```

To update the kit run:

```
bundle exec rake build && bundle exec gem install -V pkg/nabaztag_hack_kit-0.1.0.beta6.gem
```

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

The server part was heavily inspired by [Trudy.rb](https://github.com/quimarche/trudy/blob/master/trudy.rb), compiler code copied from [OpenJabNab](https://github.com/OpenJabNab/OpenJabNab). Thanks!


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
