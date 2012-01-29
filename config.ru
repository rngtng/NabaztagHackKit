#!/usr/bin/env ruby

$LOAD_PATH.unshift ::File.expand_path(::File.dirname(__FILE__) + '/lib')
require 'nabaztag_hack_kit/server'

run NabaztagHackKit::Server.new