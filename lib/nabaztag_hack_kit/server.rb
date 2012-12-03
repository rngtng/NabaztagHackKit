require 'sinatra/base'

require 'nabaztag_hack_kit/mods/callback'
# require 'nabaztag_hack_kit/mods/logger'
require 'nabaztag_hack_kit/mods/playground'

module NabaztagHackKit
  class Server < Sinatra::Base
    register Mods::Callback
    # register Mods::Logger
    register Mods::Playground

    configure :production, :development do
      enable :logging
    end

    # set :public_folder, File.dirname(__FILE__) + '../public'

    def initialize(opts = {})
      super
      @base_file     = opts.fetch(:base_file, __FILE__)
      @bytecode_file = opts.fetch(:bytecode_file, public_file('bytecode.bin'))
    end

    get "/bc.jsp" do
      if File.exists?(@bytecode_file)
        logger.info "Serving Bytecode from #{@bytecode_file}"
        send_file @bytecode_file
      else
        status 404
      end
    end

    get "/" do
      "Welcome to Nabaztag Hack Kit - it works!"
    end

    get "/favicon.ico" do
    end

    # generic catchall
    get "/*" do
      logger.warn "no route found for #{params[:splat]}"
      status 404
    end

    ####################################################################

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
