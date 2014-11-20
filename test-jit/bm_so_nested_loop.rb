#!/usr/bin/ruby
# -*- mode: ruby -*-
# $Id: nestedloop-ruby.code,v 1.4 2004/11/13 07:42:22 bfulgham Exp $
# http://www.bagley.org/~doug/shootout/
# from Avi Bryant

n = 4 # Integer(ARGV.shift || 1)
x = 0
i = 0
while i < n do
    j = 0
    while j < n do
        #k = 0
        #while k < n do
        #    l = 0
        #    while l < n do
                x += 1
        #        l += 1
        #    end
        #    k += 1
        #end
        j += 1
    end
    i += 1
end
puts x


