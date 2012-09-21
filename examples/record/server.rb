
require "nabaztag_hack_kit/server"
require "soundcloud"
require "echonest"

class Server < NabaztagHackKit::Server

  def initialize(echonest_cfg, soundcloud_cfg)
    super
    @@echonest   = Echonest(echonest_cfg[:key])
    @@soundcloud = Soundcloud.new(soundcloud_cfg)
  end

  def analyse(file_name)
    @@echonest.track.analysis(file_name)
  end

  def riff2mp3(file_name)
    `sox #{file_name} -e signed-integer out.wav && lame --quiet --alt-preset standard out.wav out.mp3`
    'out.mp3'
  end

  def norm(a, n = 255)
    min = a.min
    max = a.max - min
    a.map do |i|
      (n * ((i - min) / max)).to_i
    end
  end

  def dance(file_name)
    an = analyse riff2mp3(file_name)

    puts beats = an.tempo.to_i
    loud = an.loudness.to_i

    if beats > 0
      {
        REC_START => [3],
        EAR_L => norm(an.segments.first.timbre, 16),
        EAR_R => norm(an.segments.first.pitches, 16),
        BMP => [beats],
        LED_L1 => norm(an.segments.first.timbre, 255),
        LED_L2 => norm([beats, beats, beats, beats, loud, loud, loud, loud], 255),
        LED_L3 => norm(norm an.segments.first.pitches, 255),
      }
    else
      {
        REC_START => [3],
        EAR_L => [0],
        EAR_R => [0],
        BMP => [0],
        LED_L1 => [0],
        LED_L2 => [0],
        LED_L3 => [0]
      }
    end
  end

  def upload(file)
    @@soundcloud.post('/tracks', :track => {
      :title        => 'Bunny Boogie',
      :asset_data   => File.new(file)
    })
    {
      EAR_L  => [16,0,16,0,16,0],
      LED_L1 => [0],
      LED_L2 => [0],
      LED_L3 => [0]
    }
  end

  on :start do
    @@dance = true
    if @@recording
      @@recording = false
    end
    send_nabaztag({
      REC_STOP => []
    })
  end

  on "button-pressed" do
    send_nabaztag if @@recording
      @@recording = false
      {
        REC_STOP => [],
        LED_L1   => [100,0,0,0],
        LED_L2   => [0,100,0,100],
        LED_L3   => [0,0,100,0]
      }
    else
      @@recording = true
      sec = @@dance ? 3 : 0
      {
        REC_START => [sec]
      }
    end
  end

  on "recording-finished" do |file_name|
    @@recording = false
    send_nabaztag if @@dance
      dance
    else
      upload(file_name)
    end
  end
end

