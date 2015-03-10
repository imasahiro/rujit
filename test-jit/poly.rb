class C1
  def m
    1
  end
end
class C2
  def m
    2
  end
end

o1 = C1.new
o2 = C2.new

i = 0
j = 0
while i<10*4 # benchmark loop 2
  o = (i % 2 == 0) ? o1 : o2
  j += o.m;
  j += o.m
  i += 1
end
puts i == 10*4
puts j == 30*4
