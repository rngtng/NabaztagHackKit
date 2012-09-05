require 'nabaztag_hack_kit/server'
require 'nabaztag_hack_kit/message/helper'

require './graphite_stats'

class Server < NabaztagHackKit::Server
  include GraphiteStats

  def initialize
    super #('bytecode.bin')
  end

  on "button-pressed" do |data, request|
    send_nabaztag OK
    # ({
    #   LED_L1   => [0],
    #   LED_L2   => [0],
    #   LED_L3   => [0],
    #   LED_L4   => [0,0,0,0,100],
    # })
  end

  on "ping" do |data, request|
    send_nabaztag begin
     if payment(GraphiteStats::KEY, params[:p])
        NabaztagHackKit::Message::Helper::wink.merge(
          NabaztagHackKit::Message::Helper::circle
        )
      else
        NabaztagHackKit::Message::Helper::stop
      end
    end
  end
end

