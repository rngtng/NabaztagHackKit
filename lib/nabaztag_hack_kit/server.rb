require 'sinatra/base'

require 'nabaztag_hack_kit/message'
require 'nabaztag_hack_kit/message/api'


module NabaztagHackKit
  class Server < Sinatra::Base
    include Message::Api

    REC_FILE = "rec.wav"
    PREFIX   = "/api"

    def initialize(bytecode_path = nil)
      super
      @bytecode_path = bytecode_path || File.join('public', 'bytecode.bin')
      puts "Serving Bytecode from #{@bytecode_path}"
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

    get "/" do
      File.read(File.join('public', 'index.html'))
    end

    get "/bc.jsp" do
      # TODO recompile if changed
      send_file @bytecode_path
    end

    post "#{PREFIX}/recording-finished.jsp" do
      File.open(REC_FILE, "w+") do |f|
        f.write request.body.read
      end
      callback('recording-finished', REC_FILE)
    end

    get "#{PREFIX}/rfid.jsp" do
      callback('rfid', params[:id])
    end

    post "#{PREFIX}/log.jsp" do
      @logs = parse_log params[:logs]
      puts "#########################"
      puts @logs.join("\n")
      puts "#########################"
      send_nabaztag OK
    end

    get "#{PREFIX}/:action.jsp" do
      callback(params[:action], params, request)
    end

    post "#{PREFIX}/:action.jsp" do
      callback(params[:action], params, request)
    end
  end

end
