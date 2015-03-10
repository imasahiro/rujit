a = [nil]
idx = 0
n   = 100

def f(a, idx, n)
    i = 0
    while i < 1000000000
        a[idx] ||= n
        i += 1
    end

    a
end

f(a, idx, n)
puts a[0] # == n
