# Rake tasks to compile bytecode on remote server.
#
# E.g. you are developing on MacOS X and need a linux machine to compile
#

HOST       = "ssh-21560@warteschlange.de"
REMOTE     = "/kunden/warteschlange.de/.tmp/OpenJabNab/bootcode"
REMOTE_BIN = "compiler/mtl_linux/"

namespace :bytecode do
  namespace :remote do
    desc "Merge and Compile a file remotely, pass KEEP=1 to not cleanup"
    task :compile => :merge do
      `scp #{TMP} #{HOST}:#{REMOTE}/#{TMP}`
      `rm #{TMP}` unless ENV['KEEP']
      puts `ssh #{HOST} "cd #{REMOTE} && rm -f #{OUT} && #{REMOTE_BIN}#{COMP} -s #{TMP} #{OUT} 2>&1 #{FILTER}"`
      `scp #{HOST}:#{REMOTE}/#{OUT} #{OUT}`
    end

    desc "Merge and Run a file remotely, pass KEEP=1 to not cleanup"
    task :run => :merge do
      `scp #{TMP} #{HOST}:#{REMOTE}/#{TMP}`
      `rm #{TMP}` unless ENV['KEEP']
      puts `ssh #{HOST} "cd #{REMOTE} && #{REMOTE_BIN}#{SIMU} --source #{TMP} < #{TMP} 1>&2"`
    end
  end
end