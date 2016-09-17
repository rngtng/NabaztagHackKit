require "nabaztag_hack_kit/server"
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
