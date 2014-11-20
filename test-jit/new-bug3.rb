
def mkmatrix(rows, cols)
    count = 1
    mx = Array.new(rows)
    (0 .. (rows - 1)).each do |bi|
        row = Array.new(cols, 0)
        # mx[bi] = row
    end
    mx
end

m1 = mkmatrix(40, 40)
