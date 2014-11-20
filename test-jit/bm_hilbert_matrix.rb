# Submitted by M. Edward (Ed) Borasky
require 'mathn' # also brings in matrix, rational and complex

# return a Hilbert matrix of given dimension
def hilbert(dimension)
  rows = Array.new
  (1..dimension).each do |i|
    row = Array.new
    (1..dimension).each do |j|
      row.push(1/(i + j - 1))
    end
    rows.push(row)
  end
  return(Matrix.rows(rows))
end
def run_hilbert(dimension)
  m = hilbert(dimension)
  print "Hilbert matrix of dimension #{dimension} times its inverse = identity? "
  k = m * m.inv
  print "#{k==Matrix.I(dimension)}\n"
  m = nil # for the garbage collector
  k = nil
end

[10, 20, 30, 40, 50, 60].each do |n|
  run_hilbert(n)
end
