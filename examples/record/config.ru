#!/usr/bin/env ruby
# frozen_string_literal: true

require 'bundler/setup'
require './server'

# use Rack::Reloader, 0

ECHONEST_CFG = {
  key: ''
}.freeze

SOUNDCLOUD_CFG = {
  client_id: '',
  client_secret: '',
  username: '',
  password: ''
}.freeze

run Record::Server.new(ECHONEST_CFG, SOUNDCLOUD_CFG)
