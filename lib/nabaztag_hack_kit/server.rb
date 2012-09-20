require 'sinatra/base'

require 'nabaztag_hack_kit/message'
require 'nabaztag_hack_kit/message/api'

module NabaztagHackKit
  class Server < Sinatra::Base
    include Message::Api

    configure :production, :development do
      enable :logging
    end

    PREFIX = "/api/:bunnyid"

    def initialize(bytecode_path = nil)
      super
      @bytecode_path = bytecode_path || File.expand_path(File.join('../', 'public', 'bytecode.bin'), __FILE__)
    end

    class << self
      def callbacks
        @@callbacks ||= {}
      end

      def on(callback, &block)
        callbacks[callback] = block
      end
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

    def callback(action, data, request)
      if callback = self.class.callbacks[action.to_s]
        instance_exec data, request, &callback
      else
        # puts "no callback found for #{action}"
        send_nabaztag OK
      end
    end

    get "/" do
      File.read File.expand_path(File.join('../', 'public', 'index.html'), __FILE__)
    end

    get "/bc.jsp" do
      logger.info "Serving Bytecode from #{@bytecode_path}"
      send_file @bytecode_path
    end

    post "#{PREFIX}/log.jsp" do
      @logs = parse_log params[:logs]
      puts "#########################"
      puts @logs.join("\n")
      puts "#########################"
      send_nabaztag OK
    end

    post "#{PREFIX}/recording-finished.jsp" do
      file_name = REC_FILE # TODO add timestamp??
      File.open(file_name, "w+") do |f|
        f.write request.body.read
      end
      callback('recording-finished', file_name, request)
    end

    get "#{PREFIX}/rfid.jsp" do
      callback('rfid', params[:id], request)
    end

    get "#{PREFIX}/button-pressed.jsp" do
      callback('button-pressed', params[:duration], request)
    end

    # generic callback
    %w(get post).each do |method|
      send(method, "#{PREFIX}/:action.jsp") do
        callback(params[:action], params, request)
      end
    end

    # generic catchall
    get "/*" do
      logger.warn "no route found for #{params[:splat]}"
      status 404
    end
  end
end
