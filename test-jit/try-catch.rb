def hoge(n)
    raise TypeError, "Type error." unless n.is_a? Integer
    n / 0
end

begin
    hoge(0)
    # ZeroDivisionErrorを捕捉
rescue ZeroDivisionError => e
    puts  "error, #{e.message}"
end

#begin
#    hoge('a')
#    # TypeErrorを捕捉
#rescue TypeError => e
#    puts "error, #{e.message}"
#end
#
#begin
#    hoge('a')
#    # ZeroDivisionErrorとTypeErrorを捕捉
#rescue ZeroDivisionError, TypeError => e
#    puts "error, #{e.message}"
#end
