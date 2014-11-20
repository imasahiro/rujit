require 'webrick'

server = WEBrick::HTTPServer.new({:BindAddress => '127.0.0.1',
                                  :Port => 10080})

count = 0
server.mount_proc("/") do |req, res|
       res.content_type="text/html"
       res.body = <<EOT
<html>
<head><title>Test</title></head>
<body>Hello, WRBrick #{count}</body>
</html>
EOT
       count += 1
end

trap(:INT){server.shutdown}
server.start
