# Nabaztag Hack Kit

Everything you need to hack the Rabbit: a sinatra server including simple api framework to run custom bytecode on Nabaztag v1/v2. Includes original compiler sources for linux and a modified mac os x version.

![](http://github.com/rngtng/NabaztagHackKit.png)

## Getting Started

### Installation

The Hack Kit is distributed as a ruby gem. It comes with a simple web server (based on sinatra) which runs out-of-the for connecting you rabbit and distributing the nabaztag bytecode. In addition it includes sinatra helpers/modules to communicate with the rabbit easily. Lastly it provides binaries to compile your own Nabaztag bytecode (see **Binaries** below).

#### Simple Server

The Server is the communication endpoint for the rabbit. Its two main purposes are:

  1. serving the bytecode on bootup
  2. receive and respond to HTTP requests in a defined format.

#### Setup

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


### Understanding the Bytecode

The bytecode is written in a custom language by Sylvain Huet. It is referenced as _Metal_ and files end with `.mtl`. Unfortunately documentation is very poor (and in french). Check directory `ext/bytecode/` which contains a basic overview & documentaion as well as a list of (common) commands. A good reference is the original bytecode, included in the directory as well. Major parts got extracted into seperate files, found in `lib/` directory and ready to be included in your code.

https://docs.google.com/document/d/1KMg2wSyMKTmsilCpOByi_59uk5dD8XMfGAu20W63kZE/edit?hl=en_US

### Testing

The kit includes a simple test framework to test custom bytecode. See `test/bytecode/test.mtl`. A typical test looks like this:

```c
 let test "<test name>" -> t in
  (
    //assertions
    assert_equalI 0   10 - (2* 5);
  0);
```

The framework offers assertions similar to [Ruby Test::Unit](http://ruby-doc.org/stdlib-1.9.3/libdoc/test/unit/rdoc/Test/Unit.html) style. Mind that the variable type has to be given
explicit. Convention is:

  * I = interger
  * S = string
  * L = list
  * T = tab

Following assertions are available:

  * assert_equalI I I
  * assert_equalI S S
  * assert_nil I
  * assert_equalIL
  * assert_equalSL
  * assert_equalTL

## API
As example and for my own purposes I implemented a simple API to deal with RFID, LEDS, BUTTON and EARS easily.

### Input Devices

#### RFID
NabaztagInjector


#### BUTTON
Current Button has very basic functionality: a short press send HTTP Request of type `Log` to server, a long
press foreces the bunny to restart.

### Output Devices
Data for all output devices are stored in buffers. Each device has two: one for ontime, imediate playback, another for permanet loops.

#### LEDS
Buffers 0 - 9, where 0-4 are used for onetime, and 5-9 for loop playback.

#### EARS
Buffers 10 - 13, where 10 & 11 are used for onetime, and 12 & 13 for loop playback.



## Disclamer

The server part was heavily inspired by [Trudy.rb](https://github.com/quimarche/trudy/blob/master/trudy.rb), compiler code copied from [OpenJabNab](https://github.com/OpenJabNab/OpenJabNab). Thanks!

### Protocol
A good introduction to understand Nabaztag Protocol:

  * http://www.cs.uta.fi/hci/spi/jnabserver/documentation/index.html


### Nabaztag Background
Read following posting for more background on Nabaztag Hacking (uses google translate:)

  * [Les source bytecode et compilateur](http://translate.googleusercontent.com/translate_c?hl=en&rurl=translate.google.com&sl=fr&tl=en&twu=1&u=http://nabaztag.forumactif.fr/t13241p30-les-sources-bytecode-et-compilateur&usg=ALkJrhjLTbx1GMfSUgwhdjES1LzlE07HZQ#338060)
  * [Mindscape donne une seconde vie a nabaztag](http://translate.google.com/translate?hl=en&sl=fr&tl=en&u=http%3A%2F%2Fwww.planete-domotique.com%2Fblog%2F2011%2F08%2F07%2Fmindscape-donne-une-seconde-vie-a-nabaztag%2F)


### Future
I'd like to hack the Violet mir:ror too. Some starting points:

  * https://github.com/leh/ruby-mirror
  * http://reflektor.sourceforge.net/
  * http://www.instructables.com/id/Kids-check-in-and-check-out-with-hacked-Mirror-an/
  * http://svay.com/blog/hacking-rfid-with-nodejs/
  * http://arduino-projects4u.com/violet-mirror/


## Related Projects
First class info from the creator himself:
 -> http://www.sylvain-huet.com/?lang=en#nabv2

Nabaztag Blog
-> https://www.journaldulapin.com/tag/nabaztag/

New firmware with WPA2
1. https://github.com/ccarlo64/firmware_nabaztag
(see http://nabaztag.forumactif.fr/t15323-firmware-for-wpa-wpa2-test)

2. https://github.com/RedoXyde/nabgcc
  https://github.com/RedoXyde/mtl_linux   
  (see http://nabaztag.forumactif.fr/t15280p25-nabaztagtag-en-wpa2)

ServerlessNabaztag
-> https://github.com/andreax79/ServerlessNabaztag

CloudServer replace: http://nabaztaglives.com/


Websocket HowTo: https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers
