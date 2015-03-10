
def loop
    i = 1;
    j = 1;
    k = 0;
    while (k < 100) do
        if j < 20
            j = i
            k = k + 1
        else
            j = k
            k = k + 2
        end
    end
    j
end

puts loop()
