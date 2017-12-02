# frozen_string_literal: true

require 'json'
require 'nabaztag_hack_kit/bunny'
require 'nabaztag_hack_kit/message/helper'

module NabaztagHackKit
  module Mods
    module Playground
      module Helpers
        def commands(commands, command_values)
          Array(commands).zip(command_values).map do |command, values|
            [command] + int_array(values.split(','))
          end
        end

        def int_array(array)
          array.map do |entry|
            if entry.to_i.to_s == entry
              entry.to_i
            else
              entry
            end
          end
        end
      end

      def self.registered(app)
        app.helpers Playground::Helpers

        app.get '/' do
          redirect '/playground'
        end

        app.get '/playground' do
          File.read(public_file('index.html'))
        end

        # API
        app.get '/playground/commands' do # return list of commands
          Message::Api.constants.sort.each_with_object({}) do |constant, hash|
            if constant.to_s.length > 2
              hash[constant] = Message::Api.const_get(constant)
            end
          end.to_json
        end

        app.get '/playground/bunnies' do # return list of bunnies
          Bunny.all.to_json
        end

        app.post '/playground/bunnies/:bunnyid' do #  {"command"=>["40"], "command_values"=>[["1,2,3,4"],[]]}
          if (bunny = Bunny.find(params[:bunnyid]))
            bunny.queue_commands commands(params[:command], params[:command_values])
            bunny.to_json
          end
        end

        app.post '/playground/bunnies' do #  {"bunny"=>["0019db9c2daf"], "command"=>["40"], "command_values"=>[["1,2,3,4"],[]]}
          Array(params[:bunny]).uniq.each do |bunnyid|
            if (bunny = Bunny.find(bunnyid))
              bunny.queue_commands commands(params[:command], params[:command_values])
            end
          end

          redirect '/playground'
        end

        ##################################################################

        app.on 'ping' do |bunny|
          bunny&.next_message!
        end

        app.on 'request' do |_bunny, data|
          if (bunny = Bunny.find_or_initialize_by_id(data[:bunnyid]))
            bunny.seen!
          end
          nil # pass it on
        end
      end
    end
  end
end
