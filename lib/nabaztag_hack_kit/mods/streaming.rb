module NabaztagHackKit
  module Mods
    module Streaming

      def self.registered(app)
        app.get "/streams/:file.mp3" do
          if file = public_file("#{params[:file]}.mp3")
            File.read file
          else
            status 404
          end
        end
      end

    end
  end
end
