class C
  def m
  end
end

o = C.new

i = 0
while i<10
  i += 1
  o.__send__ :m
end
