
class Vec
  def initialize(x, y, z)
    @x = x
    @y = y
    @z = z
  end

  attr_accessor :x, :y, :z

  def vadd(b)
    Vec.new(@x + b.x, @y + b.y, @z + b.z)
  end

  def vlength
    Math.sqrt(@x * @x + @y * @y + @z * @z)
  end
end

i = 0
v = nil
l = 0
while i< 6_000_000 # benchmark loop 2
  # v = Vec.new(1, 2, 3)
  v = Vec.new(1, 2, 3).vadd(Vec.new(5, 6, 7))
  l += v.vlength
  i += 1
end
puts v.vlength
puts l
