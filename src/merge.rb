file = ARGV.first
dir = File.dirname(file)

File.open(ARGV.first).each_line do |line|
  if line =~ /\/\/include "([^"]+)"/
    line = `cat #{dir}/#{$1}.mtl`
  end
  puts line
end