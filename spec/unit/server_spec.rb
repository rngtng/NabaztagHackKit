require "spec_helper"

require "nabaztag_hack_kit/server"

describe NabaztagHackKit::Server do
  let(:app) { NabaztagHackKit::Server.new }

  describe "#bc.jsp" do
    def do_action(path)
      app.call({
        'REQUEST_METHOD' => 'GET',
        'PATH_INFO'      => path,
        'rack.input'     => StringIO.new
      }
    end

    it "reads from public/bytecode.bin" do
      app.should_receive(:send_file).with("public/bytecode.bin")
      do_action '/bc.jsp'
    end

    context "with custom bin code route" do
      let(:app) { NabaztagHackKit::Server.new(custom_route) }
      let(:custom_route) { "bytecode.bin" }

      it "reads from" do
        app.should_receive(:send_file).with(custom_route)
        do_action '/bc.jsp'
      end
    end
  end

  describe "#on" do
    it "accepts" do
    end
  end
end
