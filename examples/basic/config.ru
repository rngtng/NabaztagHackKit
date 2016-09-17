#!/usr/bin/env ruby

require 'bundler/setup'
require './server'

$stdout.sync = true

# use Rack::Reloader, 0
run Basic::Server.new
