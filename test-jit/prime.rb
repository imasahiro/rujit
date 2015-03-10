def is_prime? n
  (2...n).all? { |i| n % i != 0 }
end

def sexy_primes n
  (9..n).map do |i|
    [i - 6, i]
  end.select do |i|
    i.all? { |i| is_prime? i }
  end
end

a = Time.now
sexy_primes 100_000
b = Time.now

puts b - a
