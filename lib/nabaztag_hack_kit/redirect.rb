require 'open-uri'

module NabaztagHackKit
  class Redirect
    def initialize(app)
      @app = app
    end

    def call(env)
      path  = env['PATH_INFO']
      query = env['QUERY_STRING']
      if path =~ /\/redirect/
        req = Rack::Request.new(env)
        if (to = req.params["to"]) && !to.empty?
          @redirect = to
          render "Redirect turned on to #{@redirect}"
        else
          @redirect = nil
          render "Redirect turned off"
        end
      elsif @redirect
        render open("#{@redirect}#{path}?#{query}")
      else
        @app.call(env)
      end
    end

    private
    def render(body)
      [200, {"Content-Type" => "text/html"}, Array(body)]
    end
  end
end
