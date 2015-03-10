CharExponent = 3
BitsPerChar = 1 << CharExponent
LowMask = BitsPerChar - 1

def sieve(m)
  items = "\xFF" * ((m / BitsPerChar) + 1)

  2.step(10, 1) do |p|
    if items[p >> CharExponent][p & LowMask] == 1
    end
  end
end

sieve(100)
