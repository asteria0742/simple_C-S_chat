/*!
 * Simple chat program (server side).cpp - http://github.com/hassanyf
 * Version - 3.0.0
 *
 * Copyright (c) 2016 Hassan M. Yousuf
 */

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

using namespace std;

mutex cout_mutex;
atomic<bool> isExit(false);

void recv_thread(int sock)
{
    char buffer[1024];
    while (!isExit)
    {
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0)
        {
            if (!isExit)
            {
                lock_guard<mutex> lock(cout_mutex);
                cout << "\n=> Connection closed by client.\n";
            }
            isExit = true;
            break;
        }
        buffer[n] = '\0';

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "\rClient: " << buffer << "\nServer: " << flush;
        }

        if (string(buffer) == "#")
        {
            isExit = true;
            break;
        }
    }
}

int main()
{
    int client, server;
    int portNum = 1500;
    int bufsize = 1024;
    char buffer[bufsize];

    struct sockaddr_in server_addr;
    socklen_t size;

    client = socket(AF_INET, SOCK_STREAM, 0);

    if (client < 0)
    {
        cout << "\nError establishing socket..." << endl;
        exit(1);
    }

    cout << "\n=> Socket server has been created..." << endl;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    server_addr.sin_port = htons(portNum);

    if ((bind(client, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0)
    {
        cout << "=> Error binding connection, the socket has already been established..." << endl;
        return -1;
    }

    size = sizeof(server_addr);
    cout << "=> Looking for clients..." << endl;

    listen(client, 1);

    server = accept(client, (struct sockaddr *)&server_addr, &size);

    if (server < 0)
    {
        cout << "=> Error on accepting..." << endl;
        return -1;
    }

    strcpy(buffer, "=> Server connected...\n");
    send(server, buffer, bufsize, 0);
    cout << "=> Client connected from IP: " << inet_ntoa(server_addr.sin_addr) << endl;
    cout << "\n=> Enter # to end the connection\n" << endl;

    thread t(recv_thread, server);

    string line;
    while (!isExit)
    {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "Server: " << flush;
        }

        if (!getline(cin, line))
            break;

        if (isExit)
            break;

        send(server, line.c_str(), line.size(), 0);

        if (line == "#")
        {
            isExit = true;
            break;
        }
    }

    if (t.joinable())
        t.join();

    cout << "\n=> Connection terminated with IP " << inet_ntoa(server_addr.sin_addr);
    cout << "\nGoodbye...\n";

    close(server);
    close(client);
    return 0;
}
