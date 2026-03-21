/*!
 * Simple chat program (client side).cpp - http://github.com/hassanyf
 * Version - 3.0.0
 *
 * Copyright (c) 2016 Hassan M. Yousuf
 */

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>

using namespace std;

mutex cout_mutex;
atomic<bool> isExit(false);

// 确保发送恰好 n 字节
bool send_exact(int sock, const void* buf, size_t n)
{
    const char* ptr = (const char*)buf;
    size_t sent = 0;
    while (sent < n)
    {
        int r = send(sock, ptr + sent, n - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

// 确保接收恰好 n 字节
bool recv_exact(int sock, void* buf, size_t n)
{
    char* ptr = (char*)buf;
    size_t received = 0;
    while (received < n)
    {
        int r = recv(sock, ptr + received, n - received, 0);
        if (r <= 0) return false;
        received += r;
    }
    return true;
}

// 发送：4字节包头（消息长度） + 消息内容
bool send_msg(int sock, const string& msg)
{
    uint32_t net_len = htonl((uint32_t)msg.size());
    if (!send_exact(sock, &net_len, 4)) return false;
    if (!msg.empty() && !send_exact(sock, msg.c_str(), msg.size())) return false;
    return true;
}

// 接收：先读4字节包头得到长度，再读恰好该长度的内容
bool recv_msg(int sock, string& out)
{
    uint32_t net_len;
    if (!recv_exact(sock, &net_len, 4)) return false;
    uint32_t len = ntohl(net_len);
    out.resize(len);
    if (len > 0 && !recv_exact(sock, &out[0], len)) return false;
    return true;
}

void recv_thread(int sock)
{
    string msg;
    while (!isExit)
    {
        if (!recv_msg(sock, msg))
        {
            if (!isExit)
            {
                lock_guard<mutex> lock(cout_mutex);
                cout << "\n=> Connection closed by server.\n";
            }
            isExit = true;
            break;
        }

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "\rServer: " << msg << "\nClient: " << flush;
        }

        if (msg == "#")
        {
            isExit = true;
            break;
        }
    }
}

int main()
{
    int client;
    int portNum = 1500;
    const char* ip = "127.0.0.1";

    struct sockaddr_in server_addr;

    client = socket(AF_INET, SOCK_STREAM, 0);

    if (client < 0)
    {
        cout << "\nError establishing socket..." << endl;
        exit(1);
    }

    cout << "\n=> Socket client has been created..." << endl;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portNum);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0)
        cout << "=> Connection to the server port number: " << portNum << endl;
    else
    {
        cout << "=> Error connecting to server." << endl;
        exit(1);
    }

    cout << "=> Awaiting confirmation from the server..." << endl;
    string greeting;
    recv_msg(client, greeting);
    cout << greeting << endl;
    cout << "=> Connection confirmed, you are good to go...";
    cout << "\n\n=> Enter # to end the connection\n" << endl;

    thread t(recv_thread, client);

    string line;
    while (!isExit)
    {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "Client: " << flush;
        }

        if (!getline(cin, line))
            break;

        if (isExit)
            break;

        send_msg(client, line);

        if (line == "#")
        {
            isExit = true;
            break;
        }
    }

    if (t.joinable())
        t.join();

    cout << "\n=> Connection terminated.\nGoodbye...\n";
    close(client);
    return 0;
}
