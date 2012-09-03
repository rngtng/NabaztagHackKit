
require "nabaztag_hack_kit/server"

class Server < NabaztagHackKit::Server

  def initialize
    super('bytecode.bin')
  end

  on "button-pressed" do
    send_nabaztag({
        LED_L1   => [100,0,0,0],
        LED_L2   => [0,100,0,100],
        LED_L3   => [0,0,100,0]
    })
  end

end

