# http-proxy-server
==description==
The proxy server gets an HTTP request from the client and performs some predefined checks on it. If the request is found legal,
it first searches for the requested file in its local filesystem, if it s saved locally, the proxy creates an HTTP response and return the file,
otherwise, it forwards the request to the appropriate web server, and sends the response back to the client.
If the request is not legal, it sends an error response to the client without sending anything to the server.
Supported only IPv4 connections.

==files==
1. proxyServer.c - simple HTTP Proxy - the main program
2. threadpool.c - the code for the threadpool section(handle the threads)
3. README - description.

==remarks==
- how to compile?
gcc -Wall -Wextra -Wvla proxyServer.c threadpool.c -o proxy -lpthread

- how to run?
./proxy <port> <pool-size> <max-number-of-request> <filter>
