# -*- encoding: utf-8 -*-
$:.push File.expand_path("../lib", __FILE__)
require "open_nabaztag/version"

Gem::Specification.new do |s|
  s.name        = "open_nabaztag"
  s.version     = OpenNabaztag::VERSION
  s.authors     = ["RngTng - Tobias Bielohlawek"]
  s.email       = ["tobi@soundcloud.com"]
  s.homepage    = "https://github.com/rngtng/open_nabaztag"
  s.summary     = %q{Sinatra server to run custom Nabaztag bytecode}
  s.description = %q{Sinatra server api framework to run custom bytecode on Nabaztag v1/v2. Sources + Compiler included (linux only)}

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  s.executables   = `git ls-files -- bin/*`.split("\n").map{ |f| File.basename(f) }
  s.extensions    = ['ext/mtl_comp/extconf.rb']
  # s.executables   = ['mtl_comp', 'mtl_simu']

  s.require_paths = ["lib"]

  s.add_runtime_dependency "sinatra"
end
