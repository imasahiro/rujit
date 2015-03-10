def f(&block)
  a = [1, 2]
  j = 0
  i = 0
  while i < 10
    j += yield *a
    i += 1
  end
  j
end

v = f {|x, y|
  x + y
}

puts v
