#!ruby
#
# Enumerable.to_a defined at jit_prelude.rb do not works fine.
names = Dir.entries(".")
names.map {|n| /(?!\A)\.trans\z/ =~ n }
