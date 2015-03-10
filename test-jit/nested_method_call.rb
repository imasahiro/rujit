def f
  2
end

def g
  f
end

i = 0
j = 0
while i < 10
  i += 1
  j += g
end

puts i
puts j
