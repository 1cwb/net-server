#include "net/tcp_connection.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

#include "net/channel.h"
#include "net/event_loop.h"

namespace net {
namespace {
constexpr std::size_t kReadChunk = 16 * 1024;
}  // namespace

TcpConnection::TcpConnection(EventLoop* loop, int fd, std::string name)
    : loop_(loop), fd_(fd), name_(std::move(name)), state_(kConnecting) {
    channel_ = std::make_unique<Channel>(loop_, fd_);
    channel_->setReadCallback([this] { handleRead(); });
    channel_->setWriteCallback([this] { handleWrite(); });
    channel_->setErrorCallback([this] { handleError(); });
}

TcpConnection::~TcpConnection() {
    if (channel_) {
        if (registered_) {
            channel_->remove();
            registered_ = false;
        }
        channel_.reset();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void TcpConnection::connectEstablished() {
    if (state_ != kConnecting) return;
    setState(kConnected);
    // Tie before enabling reads so the first event cannot outlive the owner.
    channel_->tie(shared_from_this());
    channel_->enableReading();
    if (connectionCallback_) connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed() {
    if (state_ == kConnected || state_ == kDisconnecting) {
        setState(kDisconnected);
        if (channel_ && registered_) channel_->disableAll();
    }
    if (channel_ && registered_) {
        channel_->remove();
        registered_ = false;
    }
}

void TcpConnection::setState(State state) { state_ = state; }

void TcpConnection::handleRead() {
    if (state_ == kDisconnected) return;
    auto self = shared_from_this();

    char buf[kReadChunk];
    bool closed = false;
    for (;;) {
        const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            inputBuffer_.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            closed = true;
            break;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        handleError();
        return;
    }

    if (closed) {
        handleClose();
        return;
    }

    if (codec_) {
        const int rc = codec_->decode(inputBuffer_, [this, &self](const Message& msg) {
            if (state_ != kDisconnected && messageCallback_) messageCallback_(self, msg);
        });
        if (rc < 0) handleError();
        return;
    }

    if (messageCallback_ && inputBuffer_.readableBytes() > 0) {
        const Message msg{inputBuffer_.view(), 0};
        messageCallback_(self, msg);
        if (state_ != kDisconnected) inputBuffer_.retrieveAll();
    }
}

void TcpConnection::handleWrite() {
    if (state_ == kDisconnected) return;

    bool fault = false;
    while (outputBuffer_.readableBytes() > 0) {
        const ssize_t n = ::send(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes(),
                                 MSG_NOSIGNAL);
        if (n > 0) {
            outputBuffer_.retrieve(static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        fault = true;
        break;
    }

    if (fault) {
        handleError();
        return;
    }

    if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (writeCompleteCallback_) {
            auto self = shared_from_this();
            loop_->queueInLoop([self] {
                if (self->writeCompleteCallback_) self->writeCompleteCallback_(self);
            });
        }
        if (state_ == kDisconnecting) shutdownInLoop();
    }
}

void TcpConnection::handleError() { handleClose(); }

void TcpConnection::handleClose() {
    if (state_ == kDisconnected) return;
    setState(kDisconnected);
    if (channel_) channel_->disableAll();

    auto self = shared_from_this();
    if (connectionCallback_) connectionCallback_(self);
    if (closeCallback_) closeCallback_(self);
}

void TcpConnection::send(std::string_view data) {
    if (state_ == kDisconnected) return;

    if (loop_->isInLoopThread()) {
        sendInLoop(data.data(), data.size());
        return;
    }

    // Copy up front: `data` belongs to the caller and may vanish before the
    // loop thread runs the posted task.
    auto self = shared_from_this();
    std::string copy(data);
    loop_->queueInLoop([self, copy = std::move(copy)]() mutable {
        self->sendInLoop(copy.data(), copy.size());
    });
}

void TcpConnection::sendInLoop(const char* data, std::size_t len) {
    if (state_ == kDisconnected || len == 0) return;

    std::size_t remaining = len;
    std::size_t written = 0;
    bool fault = false;

    // Fast path: no queued data, so try the socket directly.
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        const ssize_t n = ::send(fd_, data, len, MSG_NOSIGNAL);
        if (n >= 0) {
            written = static_cast<std::size_t>(n);
            remaining = len - written;
            if (remaining == 0 && writeCompleteCallback_) {
                auto self = shared_from_this();
                loop_->queueInLoop([self] {
                    if (self->writeCompleteCallback_) self->writeCompleteCallback_(self);
                });
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            fault = true;
        }
    }

    if (!fault && remaining > 0) {
        outputBuffer_.append(data + written, remaining);
        if (!channel_->isWriting()) channel_->enableWriting();
    }
    if (fault) handleError();
}

void TcpConnection::shutdown() {
    if (loop_->isInLoopThread()) {
        shutdownInLoop();
    } else {
        auto self = shared_from_this();
        loop_->queueInLoop([self] { self->shutdownInLoop(); });
    }
}

void TcpConnection::shutdownInLoop() {
    if (state_ == kDisconnected || state_ == kConnecting) return;
    if (state_ == kConnected) setState(kDisconnecting);

    // Only half-close once the queued output is fully flushed. shutdown() may
    // be called before or after handleWrite drains the buffer.
    if (state_ == kDisconnecting && !shutdownWritten_ &&
        outputBuffer_.readableBytes() == 0 && channel_) {
        shutdownWritten_ = true;
        ::shutdown(fd_, SHUT_WR);
    }
}

void TcpConnection::forceClose() {
    if (loop_->isInLoopThread()) {
        handleClose();
    } else {
        auto self = shared_from_this();
        loop_->queueInLoop([self] { self->handleClose(); });
    }
}

}  // namespace net
