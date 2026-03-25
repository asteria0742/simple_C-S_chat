/*!
 * Simple chat program (server side).cpp - http://github.com/hassanyf
 * Version - 5.0.0 - epoll I/O multiplexing
 *
 * Copyright (c) 2016 Hassan M. Yousuf
 */

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
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

using namespace std;

#define MAX_EVENTS 64

// 每个客户端的连接状态（非阻塞读的状态机）
struct ClientState {
    string username;
    string ip;
    bool registered;

    // 读状态机：先读4字节包头，再读消息体
    enum Phase { HDR, BODY } phase;
    char hdr_buf[4];
    size_t hdr_read;
    uint32_t body_len;
    string body_buf;
    size_t body_read;

    explicit ClientState(const string& ip)
        : ip(ip), registered(false), phase(HDR),
          hdr_read(0), body_len(0), body_read(0) {}
};

unordered_map<int, ClientState> clients;

void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 确保发送恰好 n 字节（非阻塞版，遇到 EAGAIN 则重试）
bool send_exact(int sock, const void* buf, size_t n)
{
    const char* ptr = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(sock, ptr + sent, n - sent, MSG_NOSIGNAL);
        if (r > 0) { sent += r; continue; }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
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

void broadcast(const string& msg, int exclude_fd = -1)
{
    for (auto& kv : clients) {
        if (kv.first != exclude_fd && kv.second.registered)
            send_msg(kv.first, msg);
    }
}

void disconnect_client(int fd, int epfd)
{
    auto it = clients.find(fd);
    if (it == clients.end()) return;

    if (it->second.registered) {
        string leave_msg = "=> [" + it->second.username + "] 离开了聊天室";
        cout << leave_msg << endl;
        clients.erase(it);
        broadcast(leave_msg);
    } else {
        clients.erase(it);
    }

    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// 从非阻塞 fd 读取并处理数据，返回 false 表示应断开该连接
bool process_read(int fd, int epfd)
{
    ClientState& cs = clients.at(fd);

    while (true) {
        // 阶段1：读取4字节包头
        if (cs.phase == ClientState::HDR) {
            ssize_t r = recv(fd, cs.hdr_buf + cs.hdr_read, 4 - cs.hdr_read, 0);
            if (r > 0) {
                cs.hdr_read += r;
                if (cs.hdr_read < 4) continue; // 包头未读完，继续尝试
                // 包头完整，解析消息长度
                uint32_t net_len;
                memcpy(&net_len, cs.hdr_buf, 4);
                cs.body_len  = ntohl(net_len);
                cs.body_buf.resize(cs.body_len);
                cs.body_read = 0;
                cs.hdr_read  = 0;
                cs.phase     = ClientState::BODY;
                if (cs.body_len == 0) {
                    cs.phase = ClientState::HDR; // 空消息，跳过
                    continue;
                }
                // 立即尝试读取消息体（不等下一次 epoll 事件）
            } else if (r == 0) {
                return false; // 对端关闭连接
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 暂无更多数据
                return false;
            }
        }

        // 阶段2：读取消息体
        if (cs.phase == ClientState::BODY) {
            ssize_t r = recv(fd, &cs.body_buf[cs.body_read],
                             cs.body_len - cs.body_read, 0);
            if (r > 0) {
                cs.body_read += r;
                if (cs.body_read < cs.body_len) continue; // 消息体未读完，继续尝试
                // 消息完整，处理
                string msg    = cs.body_buf;
                cs.phase      = ClientState::HDR;
                cs.body_read  = 0;

                if (!cs.registered) {
                    // 第一条消息为用户名
                    cs.username   = msg.empty() ? "Anonymous" : msg;
                    cs.registered = true;
                    send_msg(fd, "=> 欢迎, " + cs.username + "! 输入 # 退出。");
                    string join_msg = "=> [" + cs.username + "] 加入了聊天室 (来自 " + cs.ip + ")";
                    cout << join_msg << endl;
                    broadcast(join_msg, fd);
                } else {
                    if (msg == "#") {
                        send_msg(fd, "#");
                        return false;
                    }
                    string full_msg = "[" + cs.username + "]: " + msg;
                    cout << full_msg << endl;
                    broadcast(full_msg, fd);
                }
                // 处理完一条消息后继续循环，读取可能已到达的下一条消息
            } else if (r == 0) {
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            }
        }
    }
    return true;
}

int main()
{
    int portNum = 1500;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        cout << "\nError establishing socket..." << endl;
        exit(1);
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(listen_fd);

    struct sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(portNum);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cout << "=> Error binding connection..." << endl;
        return -1;
    }

    listen(listen_fd, 128);
    cout << "\n=> Socket server has been created..." << endl;
    cout << "=> Server listening on port " << portNum
         << " (epoll), waiting for clients..." << endl;

    // 创建 epoll 实例
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];

    // 监听套接字加入 epoll
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    // stdin 加入 epoll（管理员广播）
    set_nonblocking(STDIN_FILENO);
    ev.events  = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);

    string stdin_buf;

    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) continue;

        vector<int> to_disconnect;

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                // 循环接受所有待处理的新连接
                while (true) {
                    struct sockaddr_in client_addr{};
                    socklen_t size = sizeof(client_addr);
                    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &size);
                    if (client_fd < 0) break;

                    string client_ip = inet_ntoa(client_addr.sin_addr);
                    cout << "=> New connection from " << client_ip << endl;

                    set_nonblocking(client_fd);
                    clients.emplace(client_fd, ClientState(client_ip));

                    ev.events  = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                }

            } else if (fd == STDIN_FILENO) {
                // 管理员广播：读取 stdin，按行处理
                char buf[256];
                ssize_t r;
                while ((r = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
                    stdin_buf.append(buf, r);
                size_t pos;
                while ((pos = stdin_buf.find('\n')) != string::npos) {
                    string line = stdin_buf.substr(0, pos);
                    stdin_buf.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) broadcast("[服务器]: " + line);
                }

            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                to_disconnect.push_back(fd);

            } else if (events[i].events & EPOLLIN) {
                if (!process_read(fd, epfd))
                    to_disconnect.push_back(fd);
            }
        }

        // 统一在事件循环结束后断开连接，避免在遍历过程中修改 clients
        for (int fd : to_disconnect)
            disconnect_client(fd, epfd);
    }

    close(listen_fd);
    close(epfd);
    return 0;
}
