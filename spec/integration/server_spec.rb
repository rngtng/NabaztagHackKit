require "spec_helper"

#require "rubygems"
#require "bundler/setup"

require "nabaztag_hack_kit/server"
require "rack/test"

# Add an app method for RSpec
def app
  NabaztagHackKit::Server
end

describe NabaztagHackKit::Server do
  include Rack::Test::Methods

  context "/bc.jsp" do
    let(:route) { "/bc.jsp" }

    it "returns 200" do
      get route
      last_response.should be_ok
    end

    it "reads from public/bin" do
    end

    context "with custom bin code route" do
      it "reads from" do
      end
    end
  end

  context "/vl/log.jsp" do
    let(:route) { "/vl/log.jsp" }

    it "returns 200" do
      get route
      last_response.should be_ok
    end
  end

  context "catch all route" do
    it "returns 200" do
    end

    it "execute callback" do
    end
  end

  context "callbacks" do
    it "accepts" do
    end
  end
end
