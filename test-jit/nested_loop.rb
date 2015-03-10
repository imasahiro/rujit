#!/usr/bin/ruby
# -*- mode: ruby -*-
# $Id: nestedloop-ruby.code,v 1.4 2004/11/13 07:42:22 bfulgham Exp $
# http://www.bagley.org/~doug/shootout/
# from Avi Bryant

z = 0
y = 0
x = 0
8.times do
  7.times do
    6.times do
      x += 1
    end
    y += 1
  end
  z += 1
end
puts x
puts y
puts z
