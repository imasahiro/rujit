class Integer
  def times &block
    return to_enum :times unless block_given?
    i = 0
    while i < self
      yield i
      i += 1
    end
    self
  end

  def upto(num, &block)
    return to_enum(:upto, num) unless block_given?

    i = self
    while(i <= num)
      yield i
      i += 1
    end
    self
  end

  def step(num, step, &block)
    return to_enum(:step, num, step) unless block_given?

    i = if num.kind_of? Float then self.to_f else self end
    if step > 0
      while(i <= num)
        yield i
        i += step
      end
    elsif step < 0
      while(i >= num)
        yield i
        i += step
      end
    end
    self
  end
end

class Range
  def each(&block)
    return to_enum :each unless block_given?

    i = self.begin
    unless i.respond_to? :succ
      raise TypeError, "can't iterate"
    end

    last = self.last
    return self if (i > last)

    while(i < last)
      yield i # use yield instead of `block.call`
      i = i.succ
    end

    if not exclude_end? and (i == last)
      yield i # use yield instead of `block.call`
    end
    self
  end

  def map(&block)
    return to_enum :collect unless block_given?

    ary = []
    self.each{|*val|
      ary << (yield (val))
    }
    ary
  end

  def all?(&block)
    if block_given?
      self.each{|*val|
        unless yield (val)
          return false
        end
      }
    else
      self.each{|*val|
        unless val
          return false
        end
      }
    end
    true
  end

end

class Array
  def each(&block)
    return to_enum :each unless block_given?

    idx = 0
    while(idx < length)
      elm = self[idx]
      idx += 1
      yield elm
    end
    self
  end

  def each_index(&block)
    return to_enum :each_index unless block_given?

    idx = 0
    while(idx < length)
      yield idx
      idx += 1
    end
    self
  end
  def collect(&block)
    return to_enum :collect unless block_given?

    ary = Array.new
    self.each{|e|
      v = yield e
      ary << v
    }
    ary
  end

  def collect!(&block)
    return to_enum :collect unless block_given?

    self.each_index{|idx|
      self[idx] = (yield self[idx])
    }
    self
  end
end

#module Enumerable
#  def to_a(*args)
#   ary = []
#   self.each{|*val|
#     ary.push val
#   }
#   ary
#  end
#end
