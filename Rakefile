require "bundler/gem_tasks"

require 'rspec/core/rake_task'
RSpec::Core::RakeTask.new("unit") do |t|
  t.pattern = "./spec/**/*_spec.rb" # don't need this, it's default.
end

namespace :test do
  desc "Test bytcode"
  task :bytecode do
    puts `bin/mtl_simu test/bytecode/test.mtl`
  end
end

task :spec => [:unit]
task :default => :spec

desc "start server on port 9090"
task :run do
  `rackup -p 9090`
end
