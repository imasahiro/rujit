ROW = 4
COL = 4

a = [0]
j = 0
(0...1000).each{|i|
    j += 1
    a[ROW*COL*0] = a[ROW*COL*0] = j
}

puts a[0]
puts j
