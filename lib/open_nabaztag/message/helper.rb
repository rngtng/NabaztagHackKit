module OpenNabaztag
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
      def kr(color = 0xFF, led1 = LED_L1, led2 = LED_L2, led3 = LED_L3)
        {
          led1 => [color,0,0,0],
          led2 => [0,color],
          led3 => [0,0,color,0]
        }
      end
    end
  end
end
