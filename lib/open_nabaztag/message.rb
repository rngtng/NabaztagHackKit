module OpenNabaztag
  module Message

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
    module_function(:build)

    def to_3b(int)
      [int >> 16, (int >> 8) & 0xFF, int & 0xFF]
    end
    module_function(:to_3b)

    private
    def full_message(*data)
      [0x7F] + data.flatten + [0xFF, 0x0A]
    end
    module_function(:full_message)

    def pack(message)
      message.pack('c*')
    end
    module_function(:pack)
  end

end
