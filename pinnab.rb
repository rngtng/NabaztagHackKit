require 'sinatra'

@color = []

def full_message(data)
  [0x7F] + Array(data) + [0xFF, 0x0A]
end

def message(cmd, *data)
  data = data.flatten.compact
  full_message [cmd] + [0x00, 0x00, data.size] + data
end

def ping(seconds = 30)
  message 3, seconds
end

def color
  message 2, @color
end

def reboot
  message 9
end

def send_byte_array(byte_array)
  byte_array.pack('c*')
end

##################################

get '/color' do
  if params.any?
    @color = [params["r"].to_i, params["g"].to_i, params["b"].to_i]
  end

  color.map { |t| t.to_s(16) }.join("-")
end

get '/vl/bc.jsp' do
  puts "bootcode"
  send_file File.join('public', 'bootcode', 'bootcode.bin')
end

get '/vl/rfid' do
  puts params["tag"]
  msg = (params["tag"] == "d0021a0352d64cec") ? reboot : color
  send_byte_array(msg)
end
