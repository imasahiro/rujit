def m2
  i = 0
  j = 0
  while i<30_000_00
    i += 1
    j += yield 1, 2
  end
  puts j
end

m2 do |x, y|
    x + y
end

def m3(&block)
  i = 0
  j = 0
  while i<30_000_00
    i += 1
    j += block.call(1, 2)
  end
  puts j
end

m3 do |x, y|
    x - y
end
