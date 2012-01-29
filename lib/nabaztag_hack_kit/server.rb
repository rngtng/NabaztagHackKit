require 'sinatra/base'

module NabaztagHackKit
  class Server < Sinatra::Base

    def send_nabaztag(*data)
      NabaztagHackKit::Message.build(*data)
    end

    def parse_log(logs)
      logs.to_s.split("|").map do |line|
        type, time, *values = line.split(",")
        time = time.to_i
        values = values.map &:to_i

        if type == "moved"
          values << (time - values[1]) << (values[1] - values[2]) << (time - values[2]) << (time - values[3])
        end
        "#{type}-#{time}: #{values.join("\t")}"
      end
    end

    get '/vl/bc.jsp' do
      #recompile
      puts "bootcode #{@@reboot}"
      send_file File.join('public', 'bootcode', 'bootcode.bin')
    end

    get '/vl/button.jsp' do
      send_nabaztag EAR_L => [12,0,16,0,16,0,16,0]
    end

    get '/vl/log.jsp' do
      @logs = parse_log params[:logs]
      puts "#########################"
      puts @logs.join("\n")
      puts "#########################"
      send_nabaztag OK
    end
  end

end
