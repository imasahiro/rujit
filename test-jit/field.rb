class Test
  attr_accessor :a, :b, :c, :d, :e, :f, :g
  def initialize()
    @a = 0
    @b = 0
    @c = 0
    @d = 0
    @e = 0
    @f = 0
    @g = 0
  end
end

t = Test.new
i = 0
while i < 10
  t.a += i + 0
  t.b += i + 1
  t.c += i + 2
  t.d += i + 3
  t.e += i + 4
  t.f += i + 5
  t.g += i + 6
  i += 1
end

puts t.a == 45
puts t.b == 55
puts t.c == 65
puts t.d == 75
puts t.e == 85
puts t.f == 95
puts t.g == 105
