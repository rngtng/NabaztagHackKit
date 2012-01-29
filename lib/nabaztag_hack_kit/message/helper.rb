module NabaztagHackKit
  module Message
    module Helper
      #blink
      def bl(loops = 1, color_on = 0xFF, color_off = 0x00)
        repeat(loops, [color_on, color_off])
      end

      #repeat
      def rp(loops, pattern = 0)
        Array.new(loops, pattern).flatten
      end
      alias_method :sl, :rp #sleep

      #knight rider
      def kr(color = 0xFF, led1 = Api::LED_L1, led2 = Api::LED_L2, led3 = Api::LED_L3)
        {
          led1 => [color,0,0,0],
          led2 => [0,color],
          led3 => [0,0,color,0]
        }
      end

      def fire(color = 0x110000, led1 = Api::LED_L1, led2 = Api::LED_L2, led3 = Api::LED_L3)
        data = Array.new(16) do |i|
          Message.to_3b(i * color)
        end + Array.new(8) do |i|
          Message.to_3b((15-i) * 2 * color)
        end

        {
          (led1+10) => data + [0,0,0] + [0,0,0],
          (led2+10) => [0,0,0] + data + [0,0,0],
          (led3+10) => [0,0,0] + [0,0,0] + data
        }
      end
    end
  end
end
