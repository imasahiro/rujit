def n(v, m)
  return v + m
end

def m(v)
  a = v + 1
  return n(a , 10) + 30
end
i = 0
while i<30_000_000 # while loop 1
  i += 1
  v = m(i) + 100
end

