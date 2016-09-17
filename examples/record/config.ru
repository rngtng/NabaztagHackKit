#!/usr/bin/env ruby

require 'bundler/setup'
require './server'

# use Rack::Reloader, 0

ECHONEST_CFG = {
  :key => "",
}

SOUNDCLOUD_CFG = {
  :client_id      => "",
  :client_secret  => "",
  :username       => "",
  :password       => "",
}

run Record::Server.new(ECHONEST_CFG, SOUNDCLOUD_CFG)
