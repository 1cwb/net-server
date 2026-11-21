#pragma once

#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "net/tcp_connection.h"

namespace net {

class Acceptor;
class Codec;
class EventLoop;
class EventLoopThreadPool;

// Accepts connections on one base loop and runs each connection on a worker
// loop (or the base loop when numThreads == 0).
class TcpServer {
public:
    TcpServer(EventLoop* baseLoop, std::uint16_t port, int numThreads, std::string name);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool ok() const;
    const std::string& error() const;
    std::uint16_t port() const;

    void setConnectionCallback(TcpConnection::ConnectionCallback cb);
    void setMessageCallback(TcpConnection::MessageCallback cb);
    void setWriteCompleteCallback(TcpConnection::WriteCompleteCallback cb);

    // Creates one Codec per accepted connection. Leave unset for raw TCP.
    void setCodecFactory(std::function<std::unique_ptr<Codec>()> factory);

    void start();

private:
    void newConnection(int fd, const sockaddr_in& peer);
    void removeConnection(const std::shared_ptr<TcpConnection>& conn);
    void removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);

    EventLoop* baseLoop_;
    const std::string name_;
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<EventLoopThreadPool> threadPool_;

    std::function<std::unique_ptr<Codec>()> codecFactory_;
    TcpConnection::ConnectionCallback connectionCallback_;
    TcpConnection::MessageCallback messageCallback_;
    TcpConnection::WriteCompleteCallback writeCompleteCallback_;

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> connections_;
    std::atomic<int> nextConnId_{1};
    bool started_ = false;
};

}  // namespace net
