require "spec_helper"
require "nabaztag_hack_kit/message"

describe NabaztagHackKit::Message do
  CMD  = 30
  CMD1 = 31
  let(:data) { [1,2,3,4,5] }
  let(:size) { NabaztagHackKit::Message.to_3b(data.size) }

  before do
    allow(NabaztagHackKit::Message).to receive(:full_message) { |data| data }
    allow(NabaztagHackKit::Message).to receive(:pack) { |data| data }
  end

  describe ".build" do
    it "accepts single input" do
      expect(NabaztagHackKit::Message.build(*[["40"], ["40", "2", "3", "4"]])).to eq [[40, 0, 0, 0], [40, 0, 0, 1, 50]]
    end

    it "accepts single input" do
      expect(NabaztagHackKit::Message.build(CMD)).to eq [[CMD, 0, 0, 0]]
    end

    it "accepts flat input" do
      expect(NabaztagHackKit::Message.build(CMD, *data)).to eq [[CMD, *size, *data]]
    end

    it "accepts array input" do
      expect(NabaztagHackKit::Message.build([CMD, *data])).to eq [[CMD, *size, *data]]
    end

    it "accepts multiple array input" do
      expect(NabaztagHackKit::Message.build([CMD, *data], [CMD1, *data])).to eq [[CMD, *size, *data], [CMD1, *size, *data]]
    end

    it "accepts hash input" do
      expect(NabaztagHackKit::Message.build(CMD => data, CMD1 => data)).to eq [[CMD, *size, *data], [CMD1, *size, *data]]
    end

    context "as string" do
      it "accepts hash input" do
        expect(NabaztagHackKit::Message.build(CMD.to_s => data.map(&:to_s), CMD1.to_s => data)).to eq [[CMD, *size, *data], [CMD1, *size, *data]]
      end
    end
  end

  describe ".to_3b" do
    it "accepts zero" do
      expect(NabaztagHackKit::Message.to_3b(0)).to eq [0, 0, 0]
    end

    it "accepts 2 byte input" do
      expect(NabaztagHackKit::Message.to_3b(511)).to eq [0, 1, 255]
    end

    it "accepts 3 byte input" do
      expect(NabaztagHackKit::Message.to_3b(6334239)).to eq [96, 167, 31]
    end
  end
end
