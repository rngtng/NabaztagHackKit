#!/usr/bin/env ruby

$stdout.sync = true

require 'bundler/setup'
require 'nabaztag_hack_kit/server'
require 'nabaztag_hack_kit/mods/callback'
# require 'nabaztag_hack_kit/mods/logger'
require 'nabaztag_hack_kit/mods/playground'

module Basic
  class Server < NabaztagHackKit::Server
    register NabaztagHackKit::Mods::Callback
    # register Mods::Logger
    register NabaztagHackKit::Mods::Playground
  end
end

# use Rack::Reloader, 0

file = ARGF.read.chomp
run Basic::Server.new(bytecode_file: file)
