require 'open-uri'
require 'openssl'
require 'json'

module GraphiteStats
  URL  = "https://graphite.s-cloud.net/render/?rawData=true&format=json&target=%s&from=-1minutes"
  KEY  = "stats_counts.payments.buckster-test.orders.gift_orders.shipped"
  USER = "admin"

  def get_stats(key, password)
    open(URL % key, {
      :http_basic_authentication => [USER, password],
      :ssl_verify_mode => OpenSSL::SSL::VERIFY_NONE
    }).read
  end

  def datapoints(stats)
    JSON.parse(stats).first["datapoints"].map do |point, time|
      point.to_i > 0 ? time : nil
    end.compact
  end

  def get_last_datapoint(key, password)
    if stats = get_stats(key, password)
      (datapoints(stats) - done_datapoints).pop
    end
  end

  def done_datapoints
    @@done_datapoints ||= []
  end

  def payment(key, password)
    if password && pt = get_last_datapoint(key, password)
      done_datapoints << pt
    end
  end
end
