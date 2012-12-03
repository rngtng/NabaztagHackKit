class Bunny
  @bunnies = {}

  attr_reader :id, :last_seen, :queued_commands

  class << self
    def all
      @bunnies.values
    end

    def find(id)
      @bunnies[id]
    end

    def find_or_initialize_by_id(id)
      find(id) || Bunny.new(id)
    end

    def add(bunny)
      @bunnies[bunny.id] = bunny
    end
  end

  def initialize(id)
    @id              = id
    @queued_commands = []

    Bunny.add(self)
  end

  def seen
    @last_seen = Time.now
  end

  def queue_command(command, value)
    @queued_commands << (Array(command) << value)
  end

  def to_json(a,b)
    {
      :id        => id,
      :last_seen => last_seen,
    }.to_json
  end
end
