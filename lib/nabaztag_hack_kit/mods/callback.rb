module NabaztagHackKit
  module Mods
    module Callback
      PREFIX  = "/api/:bunnyid"

      module Helpers
        include Message::Api

        def callback(action, data, request, run = 0)
          if (cb = self.class.callbacks[action.to_s]) && (callback = cb[run])
            unless instance_exec(data, request, run, &callback)
              callback(action, data, request, run+1)
            end
          else
            logger.warn "no callback found for #{action}/#{run}"
            send_nabaztag OK
          end
        end
      end

      def on(callback, &block)
        callbacks[callback] ||= []
        callbacks[callback] << block
      end

      def self.registered(app)
        app.helpers Callback::Helpers

        app.on "button-pressed" do |data, request, run|
          callback('button-pressed', params[:duration], request, run+1)
        end

        # generic api callback
        %w(get post).each do |method|
          app.send(method, "#{PREFIX}/:action.jsp") do
            callback('request', params, request)
            callback(params[:action], params, request)
          end
        end
      end

      def callbacks
        @@callbacks ||= {}
      end

    end
  end
end
