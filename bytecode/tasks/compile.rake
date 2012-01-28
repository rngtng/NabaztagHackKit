require 'stringio'

# http://rake.rubyforge.org/files/doc/rakefile_rdoc.html

COMP       = "mtl_comp"
SIMU       = "mtl_simu"
TMP        = "tmp.mtl"
OUT        = "bytecode.bin"
FILTER     = "| grep -v 'bytes' | grep -e'[a-z]'"

namespace :bytecode do
  desc "Merge a file with its includes, pass filename via FILE="
  task :merge do
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

  desc "Merge and Compile a file, pass filename via FILE="
  task :compile => :merge do
    filter_cmd = ENV['FILTER'] || args[:filter]
    `#{COMP} #{file} #{OUT} 2>&1 #{FILTER} #{filter_cmd}`
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
