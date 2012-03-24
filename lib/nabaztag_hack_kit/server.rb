require 'sinatra/base'

require 'nabaztag_hack_kit/message'
require 'nabaztag_hack_kit/message/api'


module NabaztagHackKit
  class Server < Sinatra::Base
    include Message::Api

    def initialize(bytecode_path = nil)
      super
      @bytecode_path = bytecode_path || File.join('public', 'bytecode.bin')
    end

    def send_nabaztag(*data)
      Message.build(*data)
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

    def callback(action, params, request)
      if callback = @callbacks[:action]
        send(callback, params, request)
      else

        send_nabaztag OK
      end
    end

    get '/bc.jsp' do
      # TODO recompile if changed
      send_file @bytecode_path
    end

    # post '/vl/recording.jsp' do
    #   File.open("rec.wav", "w+") do |f|
    #     f.write request.body.read
    #   end
    #   callback('recording', :file => "rec.wav")
    # end

    post '/vl/log.jsp' do
      @logs = parse_log params[:logs]
      puts "#########################"
      puts @logs.join("\n")
      puts "#########################"
      send_nabaztag OK
    end

    get '/vl/:action.jsp' do
      callback(params[:action], params, request)
    end

    post '/vl/:action.jsp' do
      callback(params[:action], params, request)
    end
  end

end
