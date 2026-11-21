#include "net/tcp_server.h"

#include <condition_variable>
#include <string>
#include <utility>
#include <vector>

#include "net/acceptor.h"
#include "net/event_loop.h"
#include "net/event_loop_thread_pool.h"

namespace net {

TcpServer::TcpServer(EventLoop* baseLoop, std::uint16_t port, int numThreads,
                     std::string name)
    : baseLoop_(baseLoop),
      name_(std::move(name)),
      acceptor_(std::make_unique<Acceptor>(baseLoop, port, 512)),
      threadPool_(std::make_unique<EventLoopThreadPool>(baseLoop, numThreads)) {
    acceptor_->setNewConnectionCallback(
        [this](int fd, const sockaddr_in& peer) { newConnection(fd, peer); });
}

TcpServer::~TcpServer() {
    // Stop accepting first, then tear down every connection on its own loop
    // while those loops are still alive, and only then join the workers.
    acceptor_.reset();

    std::vector<std::shared_ptr<TcpConnection>> connections;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections.reserve(connections_.size());
        for (auto& entry : connections_) connections.push_back(entry.second);
        connections_.clear();
    }

    if (!connections.empty()) {
        std::mutex doneMutex;
        std::condition_variable doneCv;
        int pending = static_cast<int>(connections.size());

        for (auto& conn : connections) {
            EventLoop* loop = conn->loop();
            loop->runInLoop([conn, &doneMutex, &doneCv, &pending] {
                conn->connectDestroyed();
                std::lock_guard<std::mutex> lock(doneMutex);
                --pending;
                doneCv.notify_one();
            });
        }

        std::unique_lock<std::mutex> lock(doneMutex);
        doneCv.wait(lock, [&pending] { return pending == 0; });
    }

    threadPool_->stop();
}

bool TcpServer::ok() const { return acceptor_ && acceptor_->ok(); }

const std::string& TcpServer::error() const {
    static const std::string kEmpty;
    return acceptor_ ? acceptor_->error() : kEmpty;
}

std::uint16_t TcpServer::port() const { return acceptor_ ? acceptor_->port() : 0; }

void TcpServer::setConnectionCallback(TcpConnection::ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
}

void TcpServer::setMessageCallback(TcpConnection::MessageCallback cb) {
    messageCallback_ = std::move(cb);
}

void TcpServer::setWriteCompleteCallback(TcpConnection::WriteCompleteCallback cb) {
    writeCompleteCallback_ = std::move(cb);
}

void TcpServer::setCodecFactory(std::function<std::unique_ptr<Codec>()> factory) {
    codecFactory_ = std::move(factory);
}

void TcpServer::start() {
    if (started_) return;
    started_ = true;
    threadPool_->start();
    acceptor_->listen();
}

void TcpServer::newConnection(int fd, const sockaddr_in& /*peer*/) {
    EventLoop* ioLoop = threadPool_->nextLoop();

    const std::string name = name_ + "#" + std::to_string(nextConnId_.fetch_add(1));
    auto conn = std::make_shared<TcpConnection>(ioLoop, fd, name);
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback([this](const std::shared_ptr<TcpConnection>& c) {
        removeConnection(c);
    });
    if (codecFactory_) conn->setCodec(codecFactory_());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_[name] = conn;
    }

    ioLoop->runInLoop([conn] { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn) {
    EventLoop* loop = conn->loop();
    loop->runInLoop([this, conn] { removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.erase(conn->name());
    }
    conn->connectDestroyed();
}

}  // namespace net
