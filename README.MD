ECEN5273 PA3 - HTTP Proxy Server

Instructions:
1. Unpack the tarball
2. Run the command `make` to compile the proxyserver code
3. To use the proxyserver run the following:
   1. `./proxyserver [port] [timeout_val]`
   2. [port] and [timeout_val] correspond to the port number for the proxy server and the timeout value of the webpage cache
4. To shutdown server input CTRL+C on keyboard

Explanations:

This proxy server has the following features:
1. IP Caching
2. Webpage Caching with timeout

The implementation of the code is as follows:
1. create a TCP socket listening for incoming connections with call to `int open_listenfd`
2. accept incoming connections and create a new thread for each one
3. Each thread makes a call to `void service_http_request` which does the following
   1. parse incoming HTTP requests to extract method, URI and HTTP version
   2. function call to `void parse_uri` to parse URI to get hostname, port number and path to file on end server
   3. create modified HTTP request to end server.
   4. check if hostname acquired is in cache. Calls to `void addto_ipcache` and `struct ip_cache * get_ipcache`
       1. if YES retrieve IP and skip DNS 
      2. if NO go through DNS to resolve IP
   5. Connect to end server
   6. MD5sum the request URI
   7. Check if webpage in cache; calls to `void addto_webcache` and `struct web_cache * get_webcache`
      1. if YES send cached webpage to client
      2. if NO send modified HTTP request to end server. Then retrieve the response and forward to client. Also cache the webpage as a file with filename = md5sum(URI)
   8. Close the connection for that thread
4. Server shutdown upon CTRL+C