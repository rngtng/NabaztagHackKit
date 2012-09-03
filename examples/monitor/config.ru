#!/usr/bin/env ruby

#$LOAD_PATH.unshift ::File.expand_path(::File.dirname(__FILE__) + '/lib')
require './server'

use Rack::Reloader, 0

run Server.new
