#class Range
#  def each(&block)
#    return to_enum :each unless block_given?
#
#    i = self.first
#    unless i.respond_to? :succ
#      raise TypeError, "can't iterate"
#    end
#
#    last = self.last
#    return self if (i > last)
#
#    while(i < last)
#      yield i # FIXME `block.call` is not supported
#      i = i.succ
#    end
#
#    if not exclude_end? and (i == last)
#      yield i # FIXME `block.call` is not supported
#    end
#    self
#  end
#end
#
##j = 0
##for i in 0 ... 10000000 do
##    j = i + 1;
##end
##puts j
##def g(i, &block)
##    yield i
##end
##
##def f(&block)
##    n = 0
##    m = 0
##    i = 0
##    while(i < 1000)
##        n = yield i
##        #n = g(i, &block)
##        #b = Proc.new { |i| i + 2 }
##        #n = g(i, &b)
##        i += 1
##    end
##    n
##    #i = 0
##    #while(i < 1000)
##    #    m = yield i
##    #    i += 1
##    #end
##    #return n + m
##end
##
##puts f { |i| i + 1 }
##puts f { |i| i + 2 }
##puts f { |i| i + 3 }
##puts f { |i| i + 4 }
#
sum = 0
for i in (1 .. 10)
    sum += i
end
puts sum
sum = 0
for i in (1 .. 10)
    sum += i + 1
end
puts sum
