# The Computer Language Shootout
# http://shootout.alioth.debian.org
#
# Optimized for Ruby by Jesse Millikan
# From version ported by Michael Neumann from the C gcc version,
# which was written by Christoph Bauer.

SOLAR_MASS = 4 * Math::PI**2
DAYS_PER_YEAR = 365.24

def _puts *args
end

class Planet
 def move_from_i()
 end
end

bodies = [
  # sun
  Planet.new,

  # neptune
  Planet.new
]

nbodies = bodies.size
dt = 0.01

6.times do
  i = 0
  while i < nbodies
    b = bodies[i]
    b.move_from_i()
    i += 1
  end
end
