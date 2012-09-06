module NabaztagHackKit
  module Message
    module Api
      # 0 - 10 reserved for violet backwards compatibility

      OK     = 12
      INIT   = 16
      LOG    = 17
      ERROR  = 18
      REBOOT = 19

      LED_0  = 20
      LED_1  = 21
      LED_2  = 22
      LED_3  = 23
      LED_4  = 24
      LED_L0 = 25
      LED_L1 = 26
      LED_L2 = 27
      LED_L3 = 28
      LED_L4 = 29

      EAR_L  = 40
      EAR_R  = 41
      EAR_LL = 42
      EAR_LR = 43

      REC_START = 50
      REC_STOP  = 51

      PLAY_START  = 60
      PLAY_STOP   = 61
      PLAY_LOAD   = 62
      PLAY_STREAM = 63

      F = 1
      B = 2

      def rgb(values)
        values.map do |value|
          Message.to_3b(value)
        end.flatten
      end

    end
  end
end
