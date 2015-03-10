class Vec
  def initialize()
    @x = 100
  end

  attr_accessor :x

  def vlength
    self.x * 10
  end
end

i = 0
v = nil
l = 0
while i< 6_0000 # benchmark loop 2
  v = Vec.new
  l += v.vlength
  i += 1
end
puts v.vlength
puts l
