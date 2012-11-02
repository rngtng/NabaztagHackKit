module NabaztagHackKit
  module Mods
    module Streaming

      REC_FILE = "rec.wav"

      def self.registered(app)
        app.on "recording-finished" do |data, request, run|
          file_name = REC_FILE # TODO add timestamp??
          File.open(file_name, "w+") do |f|
            f.write request.body.read
          end
          callback('recording-finished', file_name, request, run+1)
        end
      end

    end
  end
end
