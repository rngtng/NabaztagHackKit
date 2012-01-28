require 'stringio'

# http://rake.rubyforge.org/files/doc/rakefile_rdoc.html

HOST   = "ssh-21560@warteschlange.de"
REMOTE = "/kunden/warteschlange.de/.tmp/OpenJabNab/bootcode"
COMP   = "compiler/mtl_linux/mtl_comp"
SIMU   = "compiler/mtl_linux/mtl_simu"
TMP    = "tmp.mtl"
OUT    = "bytecode.bin"
FILTER = "| grep -v 'bytes' | grep -e'[a-z]'"

namespace :bytecode do
  desc "Merge a file with its includes"
  task :merge, [:file] do |t, args|
    file = ENV['FILE'] || args[:file]
    dir = File.dirname(file)
    `rm #{TMP}`
    File.open(TMP, 'w') do |out|
      File.open(file).each_line do |line|
        if line =~ /#include "([^"]+)"/
          line = `cat #{dir}/#{$1}.mtl`
        end
        out.puts line
      end
    end
  end

  desc "Merge and Compile a file"
  task :compile, [:file, :filter] => :merge do |t, args|
    filter_cmd = ENV['FILTER'] || args[:filter]
    `#{REMOTE}/#{COMP} #{file} tmp.bin 2>&1 #{FILTER} #{filter_cmd}`
  end

  desc "cleanup temporary files"
  task :clean do
    `rm -rf *.mtl *.bin`
  end

  desc "instal compiled files"
  task :install do
    `mv *.bin public/`
  end
  ###########################################################################
end
