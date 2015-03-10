items = [
  { 'id'=>1001, 'name'=>'AAA' },
  { 'id'=>1002, 'name'=>'BBB' },
  { 'id'=>1003, 'name'=>'CCC' },
  { 'id'=>1004, 'name'=>'DDD' },
  { 'id'=>1005, 'name'=>'EEE' },
  { 'id'=>1006, 'name'=>'FFF' },
  { 'id'=>1007, 'name'=>'GGG' },
  { 'id'=>1008, 'name'=>'HHH' },
  { 'id'=>1009, 'name'=>'III' },
  { 'id'=>1010, 'name'=>'JJJ' },
]

def gen_html(n, items)
  buf = nil
  n.times do
    buf = ''
    buf << <<END
<html>
  <body>
   <table>
     <thead>
       <tr>
         <th>id</th><th>name</th>
       </tr>
     </thead>
     <tbody>
END
    is_odd = false
    for item in items
      is_odd = !is_odd
      buf << "       <tr class=\"#{is_odd ? 'odd' : 'even'}\">
         <td>#{item['id']}</td><td>#{item['name']}</td>
       </tr>
"
    end
    buf << <<END
     </tbody>
   </table>
  </body>
</html>
END
  end
  buf
end

require 'benchmark'
html = nil
n = ($n || 100000).to_i
Benchmark.bm(10) do |x|
  x.report(RUBY_VERSION) { html = gen_html(n, items) }
end

#print html
#puts "Enter to exit"
#$stdin.read
