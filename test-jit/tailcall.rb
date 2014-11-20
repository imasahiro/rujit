def fact(n, acc = 1)
    if n < 2 then
        acc
    else
        fact(n-1, n*acc)
    end
end

puts(fact(36))
