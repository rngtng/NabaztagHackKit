require 'sinatra/base'

require 'nabaztag_hack_kit/message'
require 'nabaztag_hack_kit/message/api'

module NabaztagHackKit
  class Server < Sinatra::Base
    include Message::Api

    configure :production, :development do
      enable :logging
    end

    # set :public_folder, File.dirname(__FILE__) + '../public'

    REC_FILE = "rec.wav"
    PREFIX = "/api/:bunnyid"

    def initialize(opts = {})
      super
      @base_file     = opts.fetch(:base_file, __FILE__)
      @bytecode_file = opts.fetch(:bytecode_file, public_file('bytecode.bin'))
    end

    helpers do
      def bunnies
        @@bunnies ||= {}
      end

      def cmds
        @@cmds ||= {}
      end

      def commands
        Message::Api.constants.sort.inject({}) do |hash, constant|
          if constant.to_s.length > 2
            hash[constant] = Message::Api.const_get(constant)
          end
          hash
        end
      end

      def distance_of_time(from_time, to_time = Time.now)
        hours   = ( to_time - from_time) / 3600
        minutes = ((to_time - from_time) % 3600) / 60
        seconds = ((to_time - from_time) % 3600) % 60

        "".tap do |diff_in_words|
          diff_in_words << "%2dh,&nbsp;"   % hours   if hours.floor   > 0
          diff_in_words << "%2dmin,&nbsp;" % minutes if minutes.floor > 0
          diff_in_words << "%2dsec"        % seconds
        end
      end
    end

    class << self
      def callbacks
        @@callbacks ||= {}
      end

      def on(callback, &block)
        callbacks[callback] ||= []
        callbacks[callback] << block
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

    def callback(action, data, request, run = 0)
      if (cb = self.class.callbacks[action.to_s]) && (callback = cb[run])
        instance_exec data, request, run, &callback
      else
        logger.warn "no callback found for #{action}"
        send_nabaztag OK
      end
    end

    get "/" do
      erb File.read(public_file("index.html.erb"))
    end

    get "/bc.jsp" do
      if File.exists?(@bytecode_file)
        logger.info "Serving Bytecode from #{@bytecode_file}"
        send_file @bytecode_file
      else
        status 404
      end
    end

    get "/favicon.ico" do
    end

    post "/commands" do
      bunnies.each do |sn, value|
        if params[:bunny] == "*" || params[:bunny] == sn
          cmds[sn] ||= {}
          cmds[sn][params[:command].to_i] = params[:values].split(",").map(&:to_i)
        end
      end
      redirect "/"
    end

    get "/streams/:file.mp3" do
      if file = public_file("#{params[:file]}.mp3")
        File.read file
      else
        status 404
      end
    end

    # generic callback
    %w(get post).each do |method|
      send(method, "#{PREFIX}/:action.jsp") do
        bunnies[params[:bunnyid]] = Time.now
        callback(params[:action], params, request)
      end
    end

    # generic catchall
    get "/*" do
      logger.warn "no route found for #{params[:splat]}"
      status 404
    end

    # default callback
    on "log" do |data, request, run|
      parse_log(data).tap do |logs|
        logger.info "#########################"
        logger.info logs.join("\n")
        logger.info "#########################"
      end
      callback('log', data, request, run+1)
    end

    on "recording-finished" do |data, request, run|
      file_name = REC_FILE # TODO add timestamp??
      File.open(file_name, "w+") do |f|
        f.write request.body.read
      end
      callback('recording-finished', file_name, request, run+1)
    end

    on "button-pressed" do |data, request, run|
      callback('button-pressed', params[:duration], request, run+1)
    end

    on "ping" do |data, request, run|
      if cmd = cmds.delete(params[:bunnyid])
        send_nabaztag cmd
      else
        callback('ping', data, request, run+1)
      end
    end

    protected
    def public_file(name)
      public_file_path(name) || public_file_path(name, __FILE__)
    end

    def public_file_path(name, base = @base_file)
      File.expand_path(File.join('..', 'public', name), base).tap do |file|
        return file if File.exists?(file)
      end
      nil
    end
  end
end
