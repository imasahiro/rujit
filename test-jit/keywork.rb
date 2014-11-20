# def foo0
# end
# def foo3 a, b, c
# end
# def foo6 a, b, c, d, e, f
# end
def foo_kw6 k1: nil, k2: nil, k3: nil, k4: nil, k5: nil, k6: nil
end

N = 1_000_000

# N.times{
#   foo0
# }
# N.times{
#   foo3 1, 2, 3
# }
# N.times{
#   foo6 1, 2, 3, 4, 5, 6
# }
# N.times{
#   foo_kw6
# }
# N.times{
#   foo_kw6 k1: 1
# }
# N.times{
#   foo_kw6 k1: 1, k2: 2
# }
# N.times{
#   foo_kw6 k1: 1, k2: 2, k3: 3
# }
# N.times{
#   foo_kw6 k1: 1, k2: 2, k3: 3, k4: 4
# }
# N.times{
#   foo_kw6 k1: 1, k2: 2, k3: 3, k4: 4, k5: 5
# }
N.times{
  foo_kw6 k1: 1, k2: 2, k3: 3, k4: 4, k5: 5, k6: 6
}
