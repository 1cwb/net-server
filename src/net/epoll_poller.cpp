#include "net/epoll_poller.h"

#include <unistd.h>

#include <cerrno>

#include "net/channel.h"

namespace net {
namespace {

std::uint32_t to_epoll_events(std::uint32_t events) {
    std::uint32_t out = EPOLLET;
    if (events & Channel::kReadEvent) out |= EPOLLIN | EPOLLRDHUP;
    if (events & Channel::kWriteEvent) out |= EPOLLOUT;
    return out;
}

}  // namespace

EpollPoller::EpollPoller() : events_(kMaxEvents) {
    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
}

EpollPoller::~EpollPoller() {
    if (epollFd_ >= 0) ::close(epollFd_);
}

void EpollPoller::poll(int timeoutMs, std::vector<Channel*>* activeChannels) {
    if (epollFd_ < 0) return;

    const int n = ::epoll_wait(epollFd_, events_.data(), static_cast<int>(events_.size()),
                               timeoutMs);
    if (n <= 0) {
        // EINTR is expected on signals; the loop simply retries.
        return;
    }

    activeChannels->reserve(activeChannels->size() + static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);
        const std::uint32_t ev = events_[i].events;

        std::uint32_t revents = Channel::kNoneEvent;
        // Treat half-close and hang-up as readable so the owner drains the
        // socket; recv() returning 0 is the single, reliable close signal.
        if (ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) revents |= Channel::kReadEvent;
        if (ev & EPOLLOUT) revents |= Channel::kWriteEvent;
        if (ev & EPOLLERR) revents |= Channel::kErrorEvent;

        channel->setRevents(revents);
        activeChannels->push_back(channel);
    }
}

void EpollPoller::updateChannel(Channel* channel) {
    if (epollFd_ < 0) return;

    epoll_event ev{};
    ev.events = to_epoll_events(channel->events());
    ev.data.ptr = channel;

    const int op = channels_.count(channel->fd()) != 0 ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epollFd_, op, channel->fd(), &ev) == 0) {
        channels_[channel->fd()] = channel;
        registered_.insert(channel);
    }
}

void EpollPoller::removeChannel(Channel* channel) {
    auto it = channels_.find(channel->fd());
    if (it == channels_.end()) return;
    if (epollFd_ >= 0) {
        ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel->fd(), nullptr);
    }
    registered_.erase(channel);
    channels_.erase(it);
}

bool EpollPoller::hasChannel(const Channel* channel) const {
    return registered_.count(channel) != 0;
}

}  // namespace net
