module NabaztagHackKit
  module Mods
    module Playground

      module Helpers
        def bunnies
          @@bunnies ||= {}
        end

        def cmds
          @@cmds ||= {}
        end

        def bunny_seen(sn)
          bunnies[sn] = Time.now
        end

        def add_commands(bunny_sn, command, values)
          bunnies.each do |sn, value|
            if params[:bunny] == "*" || params[:bunny] == sn
              cmds[sn] ||= {}
              cmds[sn][command] = values
            end
          end
        end

        def commands
          Message::Api.constants.sort.inject({}) do |hash, constant|
            if constant.to_s.length > 2
              hash[constant] = Message::Api.const_get(constant)
            end
            hash
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
          erb File.read(public_file("index.html.erb"))
        end

        app.post "/commands" do
          add_commands(params[:bunny], params[:command].to_i, params[:values].to_s.split(",").map(&:to_i) )
          redirect "/"
        end

        ##################################################################

        app.on "ping" do |data, request, run|
          if cmd = cmds.delete(params[:bunnyid])
            send_nabaztag cmd
          else
            callback('ping', data, request, run+1)
          end
        end

        app.on 'request' do
          bunny_seen(params[:bunnyid])
          nil #pass it own
        end
      end

    end
  end
end
