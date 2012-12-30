require "spec_helper"

require "nabaztag_hack_kit/message"

describe NabaztagHackKit::Message do
  CMD  = 30
  CMD1 = 31
  let(:data) { [1,2,3,4,5] }
  let(:size) { NabaztagHackKit::Message.to_3b(data.size) }

  before do
    NabaztagHackKit::Message.stub!(:full_message).and_return do |data|
      data
    end

    NabaztagHackKit::Message.stub!(:pack).and_return do |data|
      data
    end
  end

  describe ".build" do
    it "accepts single input" do
      NabaztagHackKit::Message.build(*[["40"], ["40", "2", "3", "4"]]).should == [[40, 0, 0, 0], [40, 0, 0, 1, 50]]
    end

    it "accepts single input" do
      NabaztagHackKit::Message.build(CMD).should == [[CMD, 0, 0, 0]]
    end

    it "accepts flat input" do
      NabaztagHackKit::Message.build(CMD, *data).should == [[CMD, *size, *data]]
    end

    it "accepts array input" do
      NabaztagHackKit::Message.build([CMD, *data]).should == [[CMD, *size, *data]]
    end

    it "accepts multiple array input" do
      NabaztagHackKit::Message.build([CMD, *data], [CMD1, *data]).should == [[CMD, *size, *data], [CMD1, *size, *data]]
    end

    it "accepts hash input" do
      NabaztagHackKit::Message.build(CMD => data, CMD1 => data).should == [[CMD, *size, *data], [CMD1, *size, *data]]
    end

    context "as string" do
      it "accepts hash input" do
        NabaztagHackKit::Message.build(CMD.to_s => data.map(&:to_s), CMD1.to_s => data).should == [[CMD, *size, *data], [CMD1, *size, *data]]
      end
    end
  end

  describe ".to_3b" do
    it "accepts zero" do
      NabaztagHackKit::Message.to_3b(0).should == [0, 0, 0]
    end

    it "accepts 2 byte input" do
      NabaztagHackKit::Message.to_3b(511).should == [0, 1, 255]
    end

    it "accepts 3 byte input" do
      NabaztagHackKit::Message.to_3b(6334239).should == [96, 167, 31]
    end
  end
end
