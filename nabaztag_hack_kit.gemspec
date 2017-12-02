# -*- encoding: utf-8 -*-
$:.push File.expand_path("../lib", __FILE__)
require "nabaztag_hack_kit/version"

Gem::Specification.new do |s|
  s.name        = "nabaztag_hack_kit"
  s.version     = NabaztagHackKit::VERSION
  s.authors     = ["RngTng - Tobias Bielohlawek"]
  s.email       = ["tobi@rngtng.com"]
  s.homepage    = "https://github.com/rngtng/NabaztagHackKit"
  s.summary     = %q{Sinatra server to run custom Nabaztag bytecode}
  s.description = %q{Sinatra server api framework to run custom bytecode on Nabaztag v1/v2. Sources + Compiler included (linux only)}

  s.files         = `git ls-files --recurse-submodules`.split("\n")
  s.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  s.extensions    = ['ext/extconf.rb']

  s.bindir        = 'ext/bin'
  s.executables   = ['mtl_comp', 'mtl_simu', 'mtl_merge']

  s.require_paths = ["lib"]

  s.add_dependency 'sinatra', '>= 2.0'
  s.add_dependency 'rack', '>= 2.0'

  s.add_development_dependency 'rake', '>= 11.2'
  s.add_development_dependency 'rspec', '>= 3.5'
  s.add_development_dependency 'rack-test', '>= 0.6'
end
