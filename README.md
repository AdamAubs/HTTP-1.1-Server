# HTTP/1.1 Server — UIC CS 361 (Spring 2025)

This is a simple HTTP/1.1 server built as part of UIC’s CS 361 course to help me learn Linux system calls, specifically those used for socket-based communication. The project also introduced me to handling multiple client requests concurrently using Epoll, Linux's I/O multiplexing mechanism.

## Supported Endpoints

- `GET /hello` — Returns a greeting message to the client.

- `GET /headers` — Returns the HTTP request headers sent by the client.

- `POST /data` — Accepts and stores data sent by the client in a server-side buffer.

- `GET /stored` — Returns the previously stored data from the buffer.

- `GET /<PATH>` — Serves a file from disk that matches the path specified in the URL.

## Use of epoll

The server uses epoll to efficiently handle multiple concurrent client connections. This is especially useful for serving large files, as it allows the server to avoid blocking on a single request. Instead of processing one request at a time, epoll enables the server to respond to multiple clients asynchronously, improving responsiveness and scalability.
