
def f
  [10, 20]
end

i = 0
x = 0
y = 0
while i < 10
  a, b = f
  x += a
  y += b
  i += 1
end

puts i
puts x
puts y
