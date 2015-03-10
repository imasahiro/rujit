A = "a"
B = "b"
LINE = ["0", "1", "2", "3", "4", "5", "a"];
def read_line(i)
    if i < LINE.length
        return LINE[i]
    end
    ""
end

def f
    i = 0
    while line = read_line(i)
        if /(#{A})/ =~ line
            break
        end
        puts line
        i += 1
    end

    puts i
end

f
