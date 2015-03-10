sum = 0
sum_i = 0
sum_j = 0
3.times{|ib|
  2.times{|jb|
    sum_i += ib
    sum_j += jb
    sum += ib + jb
  }
}
puts sum
puts sum_i
puts sum_j

