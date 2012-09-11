#!/usr/bin/env ruby

$LOAD_PATH.unshift ::File.expand_path(::File.dirname(__FILE__) + '/lib')
require 'nabaztag_hack_kit/server'
require 'nabaztag_hack_kit/redirect'

$stdout.sync = true

use Rack::Reloader, 0
use NabaztagHackKit::Redirect

run NabaztagHackKit::Server.new
