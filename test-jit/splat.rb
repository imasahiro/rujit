i = 0
a = [1, 2, 3, 4]

def f a, b, c, d
  return a + b + c + d
end

v = 0
while i < 10
  v += f(*a)
  # f(a[0], a[1], a[2], a[3])
  i += 1
end
puts v
