require "bundler/gem_tasks"

load "bytecode/tasks/compile.rake"
load "bytecode/tasks/remote.rake"

desc "start server on port 9090"
task :run do
  `rackup -p 9090`
end
