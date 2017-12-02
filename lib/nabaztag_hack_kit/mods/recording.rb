# frozen_string_literal: true

module NabaztagHackKit
  module Mods
    module Streaming
      REC_FILE = 'rec.wav'

      def self.registered(app)
        app.on 'recording-finished' do |bunny, _data, request, run|
          file_name = REC_FILE # TODO: add timestamp??
          File.open(file_name, 'w+') do |f|
            f.write request.body.read
          end
          callback('recording-finished', bunny, file_name, request, run + 1)
        end
      end
    end
  end
end
