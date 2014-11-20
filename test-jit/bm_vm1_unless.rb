i = 0
j = 0
while i<30_000_000 # while loop 1
    if i % 2 == 0
        j += 1
    end
    i += 1
end

puts i
puts j
