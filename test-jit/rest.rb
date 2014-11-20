def f *arg
  return arg.length
end

a = 0
b = 0
c = 0
d = 0
i = 0
while i < 10
  a += f()
  b += f(1)
  c += f(2, 3)
  d += f(4, 5, 6)
  i += 1
end

puts i
puts a
puts b
puts c
puts d
