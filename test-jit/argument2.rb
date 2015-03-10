def f(a, b, c, d, e, f, g)
    return a + b
end

def g(a, b)
    return a * b
end

def h(a, b, c, d, e, f)
  # return a + b + c + d + e + f
  tmp1 = a + b
  tmp2 = c + d
  tmp3 = e + f
  return tmp1 + tmp2 + tmp3
end


i = 0
while i<30_000_000
    v = h(1, 2, 3, 4, 5, 6)
    # f(1, 2, 3, 4, 5, 6, 7)
    # v = g(4, 5)
    i += 1
end

puts v
