require "json"
require 'nabaztag_hack_kit/bunny'

module NabaztagHackKit
  module Mods
    module Playground

      module Helpers
        def cmds
          @@cmds ||= {}
        end

        def add_commands(bunny_sn, command, values)
          bunnies.each do |sn, value|
            if params[:bunny] == "*" || params[:bunny] == sn
              cmds[sn] ||= {}
              cmds[sn][command] = values
            end
          end
        end

        ########

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

      def self.registered(app)
        app.helpers Playground::Helpers

        app.get "/" do
          File.read(public_file("index.html"))
        end

        #API
        app.get "/bunnies" do
          # return list of bunnies
          Bunny.all.to_json
        end

        app.post "/bunnies/:bunnyid/command" do
          if bunny = Bunny.find(params[:bunnyid])
            bunny.queue_command(params[:command], params[:values])
          end
        end

        app.post "/commands" do
          # return list of available commands
          Message::Api.constants.sort.inject({}) do |hash, constant|
            if constant.to_s.length > 2
              hash[constant] = Message::Api.const_get(constant)
            end
            hash
          end
        end

        ##################################################################

        app.on "ping" do |data, request, run|
          if (bunny = Bunny.find(data[:bunnyid])) && bunny.queued_commands.any?
            send_nabaztag bunny.queued_commands.pop
          end
        end

        app.on 'request' do |data, request, run|
          if bunny = Bunny.find_or_initialize_by_id(data[:bunnyid])
            bunny.seen
          end
          nil #pass it own
        end
      end

    end
  end
end
