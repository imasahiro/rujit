i = 0
line = "00000000"
C1="a"
C2="b"
while i < 10
  break if /\A(#{C1}|#{C2})/o =~ line
  i += 1
end
puts i
