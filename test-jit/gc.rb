#
# http://samsaffron.com/archive/2014/04/08/ruby-2-1-garbage-collection-ready-for-production
@retained = []
@rand = Random.new(999)

MAX_STRING_SIZE = 100

def stress(allocate_count, retain_count, chunk_size)
  chunk = []
  while retain_count > 0 || allocate_count > 0
    if retain_count == 0 || (@rand.rand < 0.5 && allocate_count > 0)
      chunk << " " * (@rand.rand * MAX_STRING_SIZE).to_i
      allocate_count -= 1
      if chunk.length > chunk_size
        chunk = []
      end
    else
      @retained << " " * (@rand.rand * MAX_STRING_SIZE).to_i
      retain_count -= 1
    end
  end
end

start = Time.now
# simulate rails boot, 2M objects allocated 600K retained in memory
stress(2_000_000, 600_000, 200_000)

# simulate 100 requests that allocate 100K objects
stress(10_000_000, 0, 100_000)


puts "Duration: #{(Time.now - start).to_f}"

puts "RSS: #{`ps -eo rss,pid | grep #{Process.pid} | grep -v grep | awk '{ print $1;  }'`}"
