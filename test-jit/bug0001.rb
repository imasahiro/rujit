#!ruby
0x00.upto(0x1f) {|ch| p ch; [ch].pack("C") }
