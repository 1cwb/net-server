#pragma once

#include <vector>

namespace net {

class Channel;

class Poller {
public:
    virtual ~Poller() = default;

    // Block up to timeoutMs (negative = forever) and append ready channels.
    virtual void poll(int timeoutMs, std::vector<Channel*>* activeChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;

    // Used to skip a channel that a previous callback removed during the same
    // poll batch. Must not dereference `channel`.
    virtual bool hasChannel(const Channel* channel) const = 0;
};

}  // namespace net
