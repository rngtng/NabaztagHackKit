# frozen_string_literal: true

require 'nabaztag_hack_kit/message'

module NabaztagHackKit
  class Bunny
    @bunnies = {}

    attr_reader :id, :last_seen, :queued_commands

    class << self
      def all
        @bunnies.values
      end

      def find(id)
        @bunnies[id]
      end

      def find_or_initialize_by_id(id)
        find(id) || Bunny.new(id)
      end

      def add(bunny)
        @bunnies[bunny.id] = bunny
      end
    end

    def initialize(id)
      @id              = id
      @queued_commands = []

      Bunny.add(self)
    end

    def seen!
      @last_seen = Time.now
    end

    def queue_commands(commands)
      @queued_commands << commands
    end

    def next_message!
      Message.build(*@queued_commands.shift || Message::Api::OK)
    end

    def to_json(_state = nil, _deepth = nil)
      {
        id: id,
        last_seen: last_seen,
        queued_commands_size: queued_commands.size
      }.to_json
    end
  end
end
