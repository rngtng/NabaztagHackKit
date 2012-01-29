# Nabaztag Hack Kit

Everything you need to hack the Rabbit: a sinatra server including simple api framework to run custom bytecode on Nabaztag v1/v2. Sources + Compiler included (linux only)

## Nabaztag

### Compile

### Custom bytecode

 - doc
 - command list

### Test

## Server


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

The server party was heavily inspired by [Trudy.rb](https://github.com/quimarche/trudy/blob/master/trudy.rb), compiler code copied from OpenJabNab.
Thanks!


### Protocol
A good introduction to understand Nabaztag Protocol:

  * http://www.cs.uta.fi/hci/spi/jnabserver/documentation/index.html


### Nabaztag Background
Read following posting for more backgorund on Nabaztag Hacking (uses google translate:)

  * [Les source bytecode et compilateur](http://translate.googleusercontent.com/translate_c?hl=en&rurl=translate.google.com&sl=fr&tl=en&twu=1&u=http://nabaztag.forumactif.fr/t13241p30-les-sources-bytecode-et-compilateur&usg=ALkJrhjLTbx1GMfSUgwhdjES1LzlE07HZQ#338060)
  * [Mindscape donne une seconde vie a nabaztag](http://translate.google.com/translate?hl=en&sl=fr&tl=en&u=http%3A%2F%2Fwww.planete-domotique.com%2Fblog%2F2011%2F08%2F07%2Fmindscape-donne-une-seconde-vie-a-nabaztag%2F)


### Future
I'd like to hack the Violet mir:ror too. Some starting points:

  * https://github.com/leh/ruby-mirror
  * http://reflektor.sourceforge.net/


