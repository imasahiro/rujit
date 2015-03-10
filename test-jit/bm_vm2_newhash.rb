i = 0
a = {}
while i<6_000_000 # benchmark loop 2
  i += 1
  a = {0=>i, 1=>i + 1, 2=>i + 2, 3=>i + 3, 4=>i + 4}
end

puts a
