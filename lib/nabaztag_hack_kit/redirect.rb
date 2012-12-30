require 'open-uri'

# a rack middleware to redirect(Proxy through) certain request to another service
#
# it's the prefered solution for developing, so your bunny still points to live server but any request
# get forwarded to dev machine
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
    def render(response)
      [response.status.first.to_i, {"Content-Type" => response.content_type}, Array(response)]
    end
  end
end
