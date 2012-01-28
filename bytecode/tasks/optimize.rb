#
# Simple approach to compile code and check for unused methods
#
#

class Helper
  def self.split(io)
   [].tap do |blocks|
     current = ""
     io.each_line do |line|
       current << line
       if line.include?(";;")
         blocks << current
         current = ""
       end
     end
    end
  end

  def self.write(lines, file)
    File.open(file, 'w') do |f2|
      f2.puts lines.join
    end
  end

  def self.compiles?(file)
    ENV['FILE']   = file
    ENV['FILTER'] = "| grep done"
    Rake::Task[:compile].execute
    #.include?("done !")
  end
end

namespace :bytecode do
  desc "Check for any non used code"
  task :optimize => :merge do |t, args|
    file = ENV['FILE'] || args[:file]
    StringIO.new.tap do |out|
      lines = Helper.split(File.open(TMP))
      lines.size.times do |i|
        removed = lines[i]
        lines[i] = "// MISSING \n"
        _write(lines, "tmp2.mtl")
        if Helper.compiles?("tmp2.mtl")
            lines[i] = "\n/*\n#{removed}\n*/\n"
        else
          lines[i] = removed
        end
      end
      Helper.write(lines, "optimized.mtl")
    end
  end
end