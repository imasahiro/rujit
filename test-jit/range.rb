class XRange
    def initialize(first, last)
        @begin = first
        @end   = last
    end
    def each
        first, last = @begin, @end
        i = first
        while i < last
            yield i
            i += 1
        end
        self
    end
end

XRange.new(1, 10).each { |i| puts i }
