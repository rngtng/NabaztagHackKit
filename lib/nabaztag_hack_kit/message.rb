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
        data = convert_data(data)
        [cmd] + to_3b(data.size) + data
      }
    end

    def to_3b(int)
      [int >> 16, int >> 8, int].map { |i| i & 0xFF }
    end

    def convert_data(data)
      if data.first.is_a?(String)
        data.first.each_byte.to_a
      else
        data.flatten
      end
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
