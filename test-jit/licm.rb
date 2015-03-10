# def f(a, b, &block)
#   yield
#   return a * b
# end
#
# def g(a, b, &block)
#   a = a + b;
#   f(a, b) { a += 1; b += 2 }
#   yield
#   return a * b
# end
#
#
# i = 0
# for w in (1..30_000_000)
# # while i<30_000_000
#   j = 0
#   # f(1, 2, 3, 4, 5, 6, 7)
#   v = g(4, 5) { i += 0; j += 1 }
#   i += 1
# end
# def f(a, b, c, d, e, f, g)
#   return a * b
# end
#

def g(a, b, &block)
  a = a + b;
  yield
  return a * b
end

i = 0
# j = 0
while i<30_000_000
  # f(1, 2, 3, 4, 5, 6, 7)
  v = g(4, 5) { i += 0; }
  i += 1
  # j += 1
end
puts i
puts v
# puts j

