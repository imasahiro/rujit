def f(a, b, c, d, e, f, g)
    return a + b
end

def g(a, b)
    return a * b
end


i = 0
while i<30_000_000
    f(1, 2, 3, 4, 5, 6, 7)
    v = g(4, 5)
    i += 1
end

puts v
