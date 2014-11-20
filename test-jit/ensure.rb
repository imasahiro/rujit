def f a, b
  a + b
ensure
  a -= 20
end

i = 0
j = 0
while i < 10
  j += f(1, 2)
  i += 1
end
puts i
puts j
