b = [1, 2, 3, 4, 5, 6, 7, 8, 9].collect {|x| x + 1 }
puts b.join(",")

sum = 0
[1, 2, 3, 4, 5, 6, 7, 8, 9].each {|x|
  sum += x
}
puts sum
