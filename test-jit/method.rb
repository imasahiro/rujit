def foo0
end
def foo3 a, b, c
end
def foo6 a, b, c, d, e, f
end
def foo_kw6 k1: nil, k2: nil, k3: nil, k4: nil, k5: nil, k6: nil
end

def iter0
  yield
end

def iter1
  yield 1
end

def iter3
  yield 1, 2, 3
end

def iter6
  yield 1, 2, 3, 4, 5, 6
end

def iter_kw1
  yield k1: 1
end

def iter_kw2
  yield k1:1, k2: 2
end

def iter_kw3
  yield k1:1, k2: 2, k3: 3
end

def iter_kw4
  yield k1:1, k2: 2, k3: 3, k4: 4
end

def iter_kw5
  yield k1:1, k2: 2, k3: 3, k4: 4, k5: 5
end

def iter_kw6
  yield k1:1, k2: 2, k3: 3, k4: 4, k5: 5, k6: 6
end
