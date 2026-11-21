#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_set>
#include <vector>

namespace net {

using TimerId = std::uint64_t;

// Min-heap timer queue. All methods must run on the owning loop thread.
class TimerQueue {
public:
    TimerQueue() = default;

    TimerId addTimer(int delayMs, int intervalMs, std::function<void()> cb);
    void cancel(TimerId id);

    // Milliseconds until the next live timer, or -1 when nothing is pending.
    int nextTimeoutMs() const;
    void handleExpired();

private:
    struct Entry {
        std::chrono::steady_clock::time_point when;
        TimerId id;
        int intervalMs;
        std::function<void()> cb;
    };

    struct Later {
        bool operator()(const Entry& a, const Entry& b) const { return a.when > b.when; }
    };

    TimerId nextId_ = 1;
    std::priority_queue<Entry, std::vector<Entry>, Later> queue_;
    std::unordered_set<TimerId> cancelled_;
};

}  // namespace net
