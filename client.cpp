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
                cout << "\n=> Connection closed by server.\n";
            }
            isExit = true;
            break;
        }
        buffer[n] = '\0';

        {
            lock_guard<mutex> lock(cout_mutex);
            cout << "\rServer: " << buffer << "\nClient: " << flush;
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
    int client;
    int portNum = 1500;
    int bufsize = 1024;
    char buffer[bufsize];
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
    recv(client, buffer, bufsize, 0);
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

        send(client, line.c_str(), line.size(), 0);

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
