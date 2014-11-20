ITER = 10
def f
  j = 0
  escape = false
  for k in 0..ITER
    for i in 0..ITER
      if i == 7
        escape = true
        break
      end
      j += i
    end
  end
  puts j
  puts escape
end

f
