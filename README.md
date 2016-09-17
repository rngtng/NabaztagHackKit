# Nabaztag Hack Kit

Everything you need to hack the Rabbit: a sinatra server including simple api framework to run custom bytecode on Nabaztag v1/v2. Includes original compiler sources for linux.

![](http://travis-ci.org/rngtng/NabaztagHackKit.png)

## Getting Started

### Compile & Run

The kit comes with violet sources and binaries to compile custom Nabaztag bytecode. On a linux machine those binaries are compiled on installation of the gem. Following three binaries are available:

#### mtl_merge

Merges multiple `*.mtl` files into one. Files are included like in C: `#include "<relative path to file>"`. Output is temporary file `.tmp.mtl`.

#### mtl_comp

Compiles a `*.mtl` file. It calls `mtl_merge` before, and fallbacks to remote compiler in case binary is not found and `HOST` is given.

#### mtl_simu

Runs a `*.mtl` file. It calls `mtl_merge` before, and fallbacks to remote simulator in case binary is not found and `HOST` is given.


### Understanding the Bytecode

The bytecode is written in a custom language by Sylvain Huet. It is referenced as _Metal_ and files end with `.mtl`. Unfortunately documentation is very poor (and in french). Check directory `ext/bytecode/` which contains a basic overview & documentaion as well as a list of (common) commands. A good reference is the original bytecode, included in the directory as well. Major parts got extracted into seperate files, found in `lib/` directory and ready to be included in your code.

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


## Server

The Server is the communication endpoint for the rabbit. Its two main purposes are:

  1. serving the bytecode on bootup
  2. receive and respond to HTTP requests in a defined format.

see `examples/` for various ways on using & extending it. the usal steps are:

### Install
Install dependencies first:

```
bundle install --path=vendor/bundle
```

### Run
To start and run the server, execute:

```
bundle exec rackup -p <portnumer>
```

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

The server part was heavily inspired by [Trudy.rb](https://github.com/quimarche/trudy/blob/master/trudy.rb), compiler code copied from OpenJabNab.
Thanks!


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
