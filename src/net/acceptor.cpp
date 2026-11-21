#include "net/acceptor.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "net/channel.h"
#include "net/event_loop.h"
#include "net/sockets.h"

namespace net {

Acceptor::Acceptor(EventLoop* loop, std::uint16_t port, int backlog)
    : loop_(loop), port_(port) {
    listenFd_ = sockets::create_listener(port, backlog, &error_);
    if (listenFd_ < 0) {
        ok_ = false;
        return;
    }

    channel_ = std::make_unique<Channel>(loop_, listenFd_);
    channel_->setReadCallback([this] { handleRead(); });
    ok_ = true;
}

Acceptor::~Acceptor() {
    if (channel_) {
        channel_->disableAll();
        channel_->remove();
        channel_.reset();
    }
    if (listenFd_ >= 0) ::close(listenFd_);
}

void Acceptor::listen() {
    if (!ok_ || listening_) return;
    listening_ = true;
    channel_->enableReading();
}

void Acceptor::handleRead() {
    // Edge-triggered: drain the backlog before returning.
    for (;;) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        const int fd = ::accept4(listenFd_, reinterpret_cast<sockaddr*>(&peer), &len,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            sockets::set_nodelay(fd);
            if (callback_) callback_(fd, peer);
            continue;
        }
        if (errno == EINTR) continue;
        // EAGAIN: backlog drained. Other errors (EMFILE etc.) will surface on
        // the next edge; stopping here avoids a busy spin.
        break;
    }
}

}  // namespace net
