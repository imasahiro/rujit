class Z
end

class A < Z
    def f
        puts "a"
    end
end
class B < Z
    def f
        puts "b"
    end
end
class C < Z
    def f
        puts "c"
    end
end

a = [A.new, B.new, C.new, A.new, B.new, C.new]

def f a
    i = 0
    while i < a.length do
        a[i].f
        i = i + 1;
    end
end

f a
