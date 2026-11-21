#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "codec/codec.h"
#include "net/buffer.h"

namespace net {

class Channel;
class EventLoop;

// One accepted connection. Reads are decoded through an optional Codec, so the
// same class serves raw TCP, HTTP and MQTT.
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    enum State { kConnecting, kConnected, kDisconnecting, kDisconnected };

    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                               const Message&)>;
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;

    TcpConnection(EventLoop* loop, int fd, std::string name);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    EventLoop* loop() const { return loop_; }
    const std::string& name() const { return name_; }
    int fd() const { return fd_; }
    State state() const { return state_; }
    bool connected() const { return state_ == kConnected; }

    void setConnectionCallback(ConnectionCallback cb) { connectionCallback_ = std::move(cb); }
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setWriteCompleteCallback(WriteCompleteCallback cb) { writeCompleteCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }
    void setCodec(std::unique_ptr<Codec> codec) { codec_ = std::move(codec); }

    // Thread-safe: copies the bytes and posts to the owning loop when needed.
    void send(std::string_view data);
    void shutdown();
    void forceClose();

    // Called by TcpServer on the owning loop.
    void connectEstablished();
    void connectDestroyed();

private:
    void handleRead();
    void handleWrite();
    void handleError();
    void handleClose();
    void setState(State state);
    void sendInLoop(const char* data, std::size_t len);
    void shutdownInLoop();

    EventLoop* loop_;
    int fd_;
    const std::string name_;
    std::unique_ptr<Channel> channel_;
    std::unique_ptr<Codec> codec_;
    State state_;
    // Tracks whether the channel may still be registered with the poller. Once
    // removed it must not be touched again (the loop may already be gone).
    bool registered_ = true;
    bool shutdownWritten_ = false;

    Buffer inputBuffer_;
    Buffer outputBuffer_;

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    CloseCallback closeCallback_;
};

}  // namespace net
