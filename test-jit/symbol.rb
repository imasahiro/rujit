i = 0
sym1 = :a

while i<30_000_000 # while loop 1
  i += 1
  sym1 == :b
  sym1 != :a
end
