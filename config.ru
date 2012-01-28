#!/usr/bin/env ruby

$LOAD_PATH.unshift ::File.expand_path(::File.dirname(__FILE__) + '/lib')
require 'open_nabaztag/server'

run OpenNabaztag::Server.new