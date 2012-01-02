require 'sinatra'

def message(data)
  [0x7F] + [0x03, 0x00, 0x00, 0x01, 0x01] + [0xFF, 0x0A]
end

def send_byte_array byte_array
   byte_array.pack('c*')
 end

get '/vl/bc.jsp' do
  puts "bootcode"
  send_file File.join('public', 'bootcode', 'b3.bin')
end

get '/vl/locate.jsp' do
  puts "ping"
  "ping #{ENV['HOST']}\nbroad #{ENV['HOST']}"
end

get '/vl/rfid.jsp' do
  send_byte_array message(
    [0x01, 0x1E, 0x5B, 0x4F] +
    [0xE1, 0x0C, 0x6B, 0xCE] +
    [0x3B, 0xA7, 0x12, 0x89] +
    [0xB3, 0xCE, 0xC5]
  )
end

get '/*' do
  puts "########################"
  puts params.inspect
  puts
end

# 0x0A, 0x00, 0x00, data.size] + Array(data) 