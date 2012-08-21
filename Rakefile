require "bundler/gem_tasks"

require 'rspec/core/rake_task'
RSpec::Core::RakeTask.new("unit") do |t|
  t.pattern = "./spec/**/*_spec.rb" # don't need this, it's default.
end

namespace :test do
  desc "Test bytcode"
  task :bytecode do
    puts `bin/mtl_simu spec/bytecode/test.mtl`
  end
end

task :spec => [:unit]
task :default => :spec
