SOLAR_MASS = 4 * Math::PI**2
DAYS_PER_YEAR = 365.24

class Planet
 attr_accessor :x, :y, :z, :vx, :vy, :vz, :mass

 def initialize(x, mass)
   @x = x
   @mass = mass * SOLAR_MASS
 end

 def move_from_i(bodies, nbodies, dt, i)
  while i < nbodies
   b2 = bodies[i]
   dx = 1 - b2.x
   i += 1
  end
  dt * 1.0
 end
end

BODIES = [
  # sun
  Planet.new(0.0, 1.0),

  # jupiter
  Planet.new(
    4.84143144246472090e+00,
    9.54791938424326609e-04),

  Planet.new(
    1.53796971148509165e+01,
    5.15138902046611451e-05)
]

nbodies = BODIES.size
dt = 0.01

b = BODIES[0]
6.times do
  i = 0
  while i < nbodies
    b.move_from_i(BODIES, nbodies, dt, i + 1)
    i += 1
  end
end
