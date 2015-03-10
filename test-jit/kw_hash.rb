def f(a, b, c: 1.to_s, d:0)
  puts c.to_s
  # return a + b + c + d
end

i = 0
j = 0
while i < 10
  f(1, 2, d: 10)
  # j += f(10, 20, d: 40)
  i += 1
end
puts i
puts j
