# -*- encoding: utf-8 -*-
$:.push File.expand_path("../lib", __FILE__)
require "nabaztag_hack_kit/version"

Gem::Specification.new do |s|
  s.name        = "nabaztag_hack_kit"
  s.version     = NabaztagHackKit::VERSION
  s.authors     = ["RngTng - Tobias Bielohlawek"]
  s.email       = ["tobi@soundcloud.com"]
  s.homepage    = "https://github.com/rngtng/nabaztag_hack_kit"
  s.summary     = %q{Sinatra server to run custom Nabaztag bytecode}
  s.description = %q{Sinatra server api framework to run custom bytecode on Nabaztag v1/v2. Sources + Compiler included (linux only)}

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  s.executables   = `git ls-files -- bin/*`.split("\n").map{ |f| File.basename(f) }
  s.extensions    = ['ext/mtl/extconf.rb']
  s.executables   = ['mtl_comp', 'mtl_simu', 'mtl_merge']

  s.require_paths = ["lib"]

  ["sinatra"].each do |gem|
    s.add_dependency *gem.split(' ')
  end

  ["rake", "rspec"].each do |gem|
    s.add_development_dependency *gem.split(' ')
  end
end
