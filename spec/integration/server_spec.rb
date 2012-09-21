require "spec_helper"

require "nabaztag_hack_kit/server"
require "rack/test"

shared_examples_for :successful_route do
  it "returns 200" do
    get route
    last_response.should be_ok
  end
end

describe NabaztagHackKit::Server do
  include Rack::Test::Methods

  let(:app) { NabaztagHackKit::Server.new(:bytecode_file => __FILE__) }

  describe "/bc.jsp" do
    let(:route) { "/bc.jsp" }

    it_behaves_like :successful_route
  end

  describe "log.jsp" do
    let(:route) { "/api/bunnyid/log.jsp" }

    it_behaves_like :successful_route
  end

  describe "rfid.jsp" do
    let(:route) { "/api/bunnyid/rfid.jsp" }

    it_behaves_like :successful_route
  end

  describe "recording-finished.jsp" do
    let(:route) { "/api/bunnyid/recording-finished.jsp" }

    it_behaves_like :successful_route
  end

  describe "catch all route" do
    let(:route) { "/api/bunnyid/custom.jsp" }

    it_behaves_like :successful_route

    it "execute callback" do
    end
  end
end
