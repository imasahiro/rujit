require 'benchmark'

def foo0
  1
end

def foo3 a, b, c
  a + b + c
end

def foo6 a, b, c, d, e, f
  a + b + c + d + e + f
end

def foo3_default a, b, c = 3
  a + b + c
end

def foo6_default a = 1, b = 2, c = 3, d = 4, e = 5, f = 6
  a + b + c + d + e + f
end


N = 1_000_000
Benchmark.bm{|x|
  x.report{
    i=0
    while i < N
      foo0
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo3 1, 2, 3
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo6 1, 2, 3, 4, 5, 6
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo3_default 1, 2, 3
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo3_default 1, 2
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo6_default 1, 2, 3, 4, 5, 6
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo6_default 1, 2, 3, 4
      i += 1
    end
  }
  x.report{
    i=0
    while i < N
      foo6_default
      i += 1
    end
  }
}
