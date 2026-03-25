/*!
 * Simple chat program (client side).cpp - http://github.com/hassanyf
 * Version - 5.0.0 - epoll I/O multiplexing
 *
 * Copyright (c) 2016 Hassan M. Yousuf
 */

#include <iostream>
#include <string>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>

using namespace std;

string g_username;

void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool send_exact(int sock, const void* buf, size_t n)
{
    const char* ptr = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(sock, ptr + sent, n - sent, 0);
        if (r > 0) { sent += r; continue; }
        return false;
    }
    return true;
}

bool send_msg(int sock, const string& msg)
{
    uint32_t net_len = htonl(static_cast<uint32_t>(msg.size()));
    if (!send_exact(sock, &net_len, 4)) return false;
    if (!msg.empty() && !send_exact(sock, msg.c_str(), msg.size())) return false;
    return true;
}

// 阻塞版接收，仅用于连接握手阶段
bool recv_msg_blocking(int sock, string& out)
{
    char hdr[4];
    size_t hdr_read = 0;
    while (hdr_read < 4) {
        ssize_t r = recv(sock, hdr + hdr_read, 4 - hdr_read, 0);
        if (r <= 0) return false;
        hdr_read += r;
    }
    uint32_t net_len;
    memcpy(&net_len, hdr, 4);
    uint32_t len = ntohl(net_len);
    out.resize(len);
    size_t body_read = 0;
    while (body_read < len) {
        ssize_t r = recv(sock, &out[body_read], len - body_read, 0);
        if (r <= 0) return false;
        body_read += r;
    }
    return true;
}

// 非阻塞接收状态机（epoll 事件循环中使用）
struct RecvState {
    enum Phase { HDR, BODY } phase;
    char hdr_buf[4];
    size_t hdr_read;
    uint32_t body_len;
    string body_buf;
    size_t body_read;

    RecvState() : phase(HDR), hdr_read(0), body_len(0), body_read(0) {}
};

enum ReadResult { MSG_COMPLETE, MSG_PARTIAL, MSG_ERROR };

ReadResult try_recv(int sock, RecvState& rs, string& out)
{
    while (true) {
        if (rs.phase == RecvState::HDR) {
            ssize_t r = recv(sock, rs.hdr_buf + rs.hdr_read, 4 - rs.hdr_read, 0);
            if (r > 0) {
                rs.hdr_read += r;
                if (rs.hdr_read < 4) continue;
                uint32_t net_len;
                memcpy(&net_len, rs.hdr_buf, 4);
                rs.body_len  = ntohl(net_len);
                rs.body_buf.resize(rs.body_len);
                rs.body_read = 0;
                rs.hdr_read  = 0;
                rs.phase     = RecvState::BODY;
                if (rs.body_len == 0) { rs.phase = RecvState::HDR; continue; }
            } else if (r == 0) {
                return MSG_ERROR;
            } else {
                return (errno == EAGAIN || errno == EWOULDBLOCK) ? MSG_PARTIAL : MSG_ERROR;
            }
        }
        if (rs.phase == RecvState::BODY) {
            ssize_t r = recv(sock, &rs.body_buf[rs.body_read],
                             rs.body_len - rs.body_read, 0);
            if (r > 0) {
                rs.body_read += r;
                if (rs.body_read < rs.body_len) continue;
                out           = rs.body_buf;
                rs.phase      = RecvState::HDR;
                rs.body_read  = 0;
                return MSG_COMPLETE;
            } else if (r == 0) {
                return MSG_ERROR;
            } else {
                return (errno == EAGAIN || errno == EWOULDBLOCK) ? MSG_PARTIAL : MSG_ERROR;
            }
        }
    }
}

int main()
{
    int portNum = 1500;
    const char* ip = "127.0.0.1";

    // 连接前输入用户名（此时 stdin 仍为阻塞模式）
    cout << "=> 请输入你的用户名: ";
    getline(cin, g_username);
    if (g_username.empty()) g_username = "Anonymous";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cout << "\nError establishing socket..." << endl;
        exit(1);
    }
    cout << "\n=> Socket client has been created..." << endl;

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(portNum);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        cout << "=> Error connecting to server." << endl;
        exit(1);
    }
    cout << "=> Connection to the server port number: " << portNum << endl;

    // 握手阶段：使用阻塞 I/O 发送用户名、接收欢迎消息
    send_msg(sock, g_username);
    string greeting;
    if (!recv_msg_blocking(sock, greeting)) {
        cout << "=> Connection failed." << endl;
        exit(1);
    }
    cout << greeting << endl;
    cout << "\n=> Enter # to end the connection\n" << endl;
    cout << g_username << ": " << flush;

    // 握手完成后切换为非阻塞模式，进入 epoll 事件循环
    set_nonblocking(sock);
    set_nonblocking(STDIN_FILENO);

    int epfd = epoll_create1(0);
    struct epoll_event ev, events[2];

    ev.events  = EPOLLIN;
    ev.data.fd = sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);

    ev.events  = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);

    RecvState rs;
    string stdin_buf;
    bool running = true;

    while (running) {
        int n = epoll_wait(epfd, events, 2, -1);
        if (n < 0) break;

        for (int i = 0; i < n && running; i++) {
            int fd = events[i].data.fd;

            if (fd == STDIN_FILENO) {
                // 读取用户输入，按行处理
                char buf[256];
                ssize_t r;
                while ((r = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
                    stdin_buf.append(buf, r);

                size_t pos;
                while ((pos = stdin_buf.find('\n')) != string::npos) {
                    string line = stdin_buf.substr(0, pos);
                    stdin_buf.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    send_msg(sock, line);
                    if (line == "#") { running = false; break; }
                    cout << g_username << ": " << flush;
                }

            } else if (fd == sock) {
                // 接收服务端消息
                string msg;
                ReadResult res = try_recv(sock, rs, msg);
                if (res == MSG_ERROR) {
                    cout << "\n=> Connection closed by server.\n";
                    running = false;
                } else if (res == MSG_COMPLETE) {
                    if (msg == "#") {
                        running = false;
                    } else {
                        cout << "\r" << msg << "\n" << g_username << ": " << flush;
                    }
                }
                // MSG_PARTIAL：消息未完整到达，等待下一次 epoll 事件
            }
        }
    }

    close(epfd);
    cout << "\n=> Connection terminated.\nGoodbye...\n";
    close(sock);
    return 0;
}
