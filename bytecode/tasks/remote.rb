# Rake tasks to compile bytecode on remote server.
#
# E.g. you are developing on MacOS X and need a linux machine to compile
#

namespace :bytecode do
  namespace :remote do
    desc "Merge and Compile a file remotely"
    task :compile => :merge do |t|
      `scp #{TMP} #{HOST}:#{REMOTE}/#{TMP}`
      `rm #{TMP}` unless ENV['KEEP']
      puts `ssh #{HOST} "cd #{REMOTE} && rm -f #{OUT} && #{COMP} -s #{TMP} #{OUT} 2>&1 #{FILTER}"`
      `scp #{HOST}:#{REMOTE}/#{OUT} #{OUT}`
    end

    desc "Merge and Run a file remotely"
    task :run => :merge do |t|
      `scp #{TMP} #{HOST}:#{REMOTE}/#{TMP}`
      `rm #{TMP}` unless ENV['KEEP']
      puts `ssh #{HOST} "cd #{REMOTE} && #{SIMU} --source #{TMP} < #{TMP} 1>&2"`
    end
  end
end