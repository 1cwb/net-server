#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace net {

class Channel;
class EventLoop;

// Owns the listening socket and turns readable events into accepted fds.
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int fd, const sockaddr_in& peer)>;

    Acceptor(EventLoop* loop, std::uint16_t port, int backlog);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    std::uint16_t port() const { return port_; }

    void setNewConnectionCallback(NewConnectionCallback cb) { callback_ = std::move(cb); }
    void listen();

private:
    void handleRead();

    EventLoop* loop_;
    std::uint16_t port_;
    int listenFd_ = -1;
    bool ok_ = false;
    bool listening_ = false;
    std::string error_;
    std::unique_ptr<Channel> channel_;
    NewConnectionCallback callback_;
};

}  // namespace net
