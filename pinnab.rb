require 'sinatra'

@@reboot = ENV["NR"].nil?

def full_message(data)
  [0x7F] + Array(data) + [0xFF, 0x0A]
end

def message(cmd, *data)
  data = data.flatten.compact
  full_message [cmd] + [0x00, 0x00, data.size] + data
end

def ok
  message 3
end

def reboot
  puts "reboot"
  message 9
end

def send_byte_array(byte_array)
  byte_array.pack('c*')
end

##################################

get '/vl/bc.jsp' do
  puts "bootcode #{@@reboot}"
  @@reboot = false
  send_file File.join('public', 'bootcode', 'bootcode.bin')
end

get '/vl/*' do
  msg = @@reboot ? reboot : ok
  send_byte_array(msg)
end
