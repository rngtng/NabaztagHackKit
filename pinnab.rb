require 'sinatra'

def message(data)
  [0x7F] + Array(data) + [0xFF, 0x0A]
end

def ping(time = 30) #seconds
  message [0x03, 0x00, 0x00, 0x01, time]
end

def reboot
  message [0x09, 0x00, 0x00, 0x00]
end

def send_byte_array byte_array
  byte_array.pack('c*')
end

##################################

get '/vl/bc.jsp' do
  puts "bootcode"
  send_file File.join('public', 'bootcode', 'b3.bin')
end

get '/vl/p4.jsp' do
  puts "ping"
  send_byte_array ping
end

get '/vl/locate.jsp' do
  puts "conf"
  "ping #{ENV['HOST']}\nbroad #{ENV['HOST']}"
end

get '/vl/rfid.jsp' do
  msg = (params["t"][0] == "d0021a0352d64cec") ? reboot : ping
  send_byte_array(msg)
end

get '/*' do
  puts params.inspect
end
