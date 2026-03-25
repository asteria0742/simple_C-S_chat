# C++ Sockets - Simple server and client chat (linux)

A simple socket programming which creates a connection between two terminals on linux. This was my second semester final project, so I thought I'll share with you.

Video: https://www.youtube.com/watch?v=IydkqseK6oQ

# Requirements

1. Ubuntu 12.0 LTS or higher
2. G++ compiler for Ubuntu
3. A text editor

# Compilation

1. Compile the server.cpp file first and then the client.cpp file.
```
g++ -std=c++17 -o server server.cpp -pthread
g++ -std=c++17 -o client client.cpp -pthread
```
2. To close the connection, type a "#" and send it.

# Limitations

This code is limited to one client only. There is only one-to-one connection. To have multiple connections you need to know about threading.
