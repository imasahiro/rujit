require 'complex'

def mandelbrot? z
  i = 0
  while i<100
    i += 1
    z = z * z
    return false if z.abs > 2
  end
  true
end

mandelbrot?(Complex(1/50.0, 1/50.0))
