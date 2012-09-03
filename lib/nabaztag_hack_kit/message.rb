module NabaztagHackKit
  module Message
    extend self

    def build(*commands)
      commands = if commands.first.is_a?(Hash)
        commands.first
      elsif !commands.first.is_a?(Array)
        [commands]
      else
        commands
      end

      pack full_message commands.map { |cmd, *data|
        [cmd] + to_3b(data.flatten.size) + data.flatten
      }
    end

    def to_3b(int)
      [int >> 16, int >> 8, int].map { |i| i & 0xFF }
    end

    private
    def full_message(*data)
      [0x7F] + data.flatten + [0xFF, 0x0A]
    end

    def pack(message)
      message.pack('c*')
    end
  end

end
