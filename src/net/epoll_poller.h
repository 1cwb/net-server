#pragma once

#include <sys/epoll.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "net/poller.h"

namespace net {

// Linux epoll in edge-triggered mode.
class EpollPoller : public Poller {
public:
    EpollPoller();
    ~EpollPoller() override;

    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;

    bool ok() const { return epollFd_ >= 0; }

    void poll(int timeoutMs, std::vector<Channel*>* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    bool hasChannel(const Channel* channel) const override;

private:
    static constexpr int kMaxEvents = 256;

    int epollFd_ = -1;
    std::vector<epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
    std::unordered_set<const Channel*> registered_;
};

}  // namespace net
