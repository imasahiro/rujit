class Range
  def each(&block)
    return to_enum :each unless block_given?

    i = self.first
    unless i.respond_to? :succ
      raise TypeError, "can't iterate"
    end

    last = self.last
    return self if (i > last)

    while(i < last)
      yield i # FIXME `block.call` is not supported
      i = i.succ
    end

    if not exclude_end? and (i == last)
      yield i # FIXME `block.call` is not supported
    end
    self
  end
end

j = 0
for i in 0 ... 10000000 do
    j = i + 1;
end
puts j
