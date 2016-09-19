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
      })
    end

    xit "reads from public/bytecode.bin" do
      expect(app).to_receive(:send_file).with("public/bytecode.bin")
      begin
        do_action '/bc.jsp'
      rescue SystemExit
      end
    end

    context "with custom bin code route" do
      let(:app) { NabaztagHackKit::Server.new(custom_route) }
      let(:custom_route) { "bytecode.bin" }

      xit "reads from" do
        expect(app).to_receive(:send_file).with(custom_route)
        begin
          do_action '/bc.jsp'
        rescue SystemExit
        end
      end
    end
  end

  describe "#on" do
    xit "accepts" do
    end
  end
end
