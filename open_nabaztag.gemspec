# -*- encoding: utf-8 -*-
$:.push File.expand_path("../lib", __FILE__)
require "open_nabaztag/version"

Gem::Specification.new do |s|
  s.name        = "open_nabaztag"
  s.version     = OpenNabaztag::VERSION
  s.authors     = ["Tobias Bielohlawek"]
  s.email       = ["tobi@soundcloud.com"]
  s.homepage    = ""
  s.summary     = %q{Sinatra server to run custom Nabaztag bootcode}
  s.description = %q{Sinatra server for Nabaztag v1/v2 to run your custom Nabaztag version. Sources + Compiler included (linux only)}

  # s.rubyforge_project = "open_nabaztag"

  s.files         = `git ls-files`.split("\n")
  s.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  s.executables   = `git ls-files -- bin/*`.split("\n").map{ |f| File.basename(f) }
  s.require_paths = ["lib"]

  # specify any dependencies here; for example:
  # s.add_development_dependency "rspec"
  s.add_runtime_dependency "sinatra"
end
