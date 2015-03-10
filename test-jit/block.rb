#def m(i)
#  yield i
#end
#
#i = 0
#j = 0
#while i<30_000_000 # while loop 1
#  i += 1
#  j = m(i) {|i| i + 1}
#end
#
#puts j

def m(n, &block)
    yield n
end

j = 0

for i in (1 .. 10)
    j += m(i) { |i| i + 1 }
end

puts j
