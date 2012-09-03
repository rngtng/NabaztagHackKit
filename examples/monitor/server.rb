
require "nabaztag_hack_kit/server"

require 'nabaztag_hack_kit/message/helper'

class Server < NabaztagHackKit::Server

  def initialize
    super #('bytecode.bin')
  end

  on "button-pressed" do |data, request|
    send_nabaztag({
      LED_L1   => [0],
      LED_L2   => [0],
      LED_L3   => [0],
      LED_L4   => [0,0,0,0,100],
    })
  end

  on "ping" do |data, request|
    #send_nabaztag NabaztagHackKit::Message::Helper::fire
    # ({
    #   LED_L1   => [0],
    #   LED_L2   => [0,100, 200, 300],
    #   LED_L3   => [0],
    #   LED_L4   => [0,0,0,0,100],
    # })
  end
end

