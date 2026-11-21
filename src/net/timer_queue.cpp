#include "net/timer_queue.h"

#include <algorithm>

namespace net {

TimerId TimerQueue::addTimer(int delayMs, int intervalMs, std::function<void()> cb) {
    const TimerId id = nextId_++;
    const auto when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    queue_.push(Entry{when, id, std::max(intervalMs, 0), std::move(cb)});
    return id;
}

void TimerQueue::cancel(TimerId id) {
    if (id != 0) cancelled_.insert(id);
}

int TimerQueue::nextTimeoutMs() const {
    if (queue_.empty()) return -1;

    const auto now = std::chrono::steady_clock::now();
    if (queue_.top().when <= now) return 0;

    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(queue_.top().when - now);
    return static_cast<int>(delta.count()) + 1;
}

void TimerQueue::handleExpired() {
    const auto now = std::chrono::steady_clock::now();

    std::vector<Entry> expired;
    while (!queue_.empty() && queue_.top().when <= now) {
        Entry entry = queue_.top();
        queue_.pop();
        if (cancelled_.erase(entry.id) != 0) continue;
        expired.push_back(std::move(entry));
    }

    for (auto& entry : expired) {
        if (entry.cb) entry.cb();
        // A repeating timer is re-armed only after the callback returns, so a
        // callback that calls cancel() on itself is still honoured.
        if (entry.intervalMs > 0 && cancelled_.erase(entry.id) == 0) {
            entry.when = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(entry.intervalMs);
            queue_.push(std::move(entry));
        }
    }
}

}  // namespace net
