def fib
    a, b = 0, 1
    x = 9999**4000
    while b < x
        a, b = b, a+b
    end
    a
end

puts fib
