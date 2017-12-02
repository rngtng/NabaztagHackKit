# frozen_string_literal: true

module NabaztagHackKit
  module Mods
    module Logger
      module Helper
        def parse_log(logs)
          logs.to_s.split('|').map do |line|
            type, time, *values = line.split(',')
            time = time.to_i
            values = values.map(&:to_i)

            if type == 'moved'
              values << (time - values[1]) << (values[1] - values[2]) << (time - values[2]) << (time - values[3])
            end
            "#{type}-#{time}: #{values.join("\t")}"
          end
        end
      end

      def self.registered(app)
        app.on 'log' do |data, request, run|
          parse_log(data).tap do |logs|
            logger.info '#########################'
            logger.info logs.join("\n")
            logger.info '#########################'
          end
          callback('log', data, request, run + 1)
        end
      end
    end
  end
end
