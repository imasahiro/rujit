
class Vec
  attr_accessor :x
  def initialize()
    @x = 10
  end

end

i = 0
while i< 7000 # benchmark loop 2
  v = Vec.new
  i += 1
end
