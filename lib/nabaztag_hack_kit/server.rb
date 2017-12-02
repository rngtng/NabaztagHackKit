# frozen_string_literal: true

require 'sinatra/base'

module NabaztagHackKit
  class Server < Sinatra::Base
    configure :production, :development do
      enable :logging
    end

    # set :public_folder, File.dirname(__FILE__) + '../public'

    def initialize(opts = {})
      super
      @base_file     = opts.fetch(:base_file, __FILE__)
      @bytecode_file = opts.fetch(:bytecode_file, public_file('bytecode.bin')).to_s
      abort("Couldn't find Bytecode file. Pls compile first") unless File.exist?(@bytecode_file)
      puts "Serving Bytecode from #{@bytecode_file}"
    end

    get '/bc.jsp' do
      send_file @bytecode_file
    end

    get '/' do
      'Welcome to Nabaztag Hack Kit - it works!'
    end

    get '/favicon.ico' do
    end

    # generic catchall
    get '/*' do
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
        return file if File.exist?(file)
      end
      nil
    end
  end
end
