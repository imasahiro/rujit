def f(a)
  i = 0
  j = 0
  while i < 10
    j += a.f
    i += 1
  end
  j
end


class A
  def f
    10
  end
end

class B
  def f
    20
  end
end

puts f A.new
puts f B.new
