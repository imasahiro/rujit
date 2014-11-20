def f a, b, c, *d
  return a + b + c + d[0] + d[1]
end

a = 0
i = 0
arg = [1, 2, 3, 4, 5]
while i < 10
  a += f(*arg)
  i += 1
end

puts i
puts a
