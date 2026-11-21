#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "net/timer_queue.h"

namespace net {

class Channel;
class Poller;

// A single-threaded event loop. One loop must be created and run on the same
// thread. Cross-thread work is posted through queueInLoop(), which wakes the
// loop via an eventfd.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit();

    void runInLoop(std::function<void()> cb);
    void queueInLoop(std::function<void()> cb);

    bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    // Timers must be created from the loop thread (or before loop() starts).
    TimerId runAfter(int delayMs, std::function<void()> cb) {
        return timerQueue_->addTimer(delayMs, 0, std::move(cb));
    }
    TimerId runEvery(int intervalMs, std::function<void()> cb) {
        return timerQueue_->addTimer(intervalMs, intervalMs, std::move(cb));
    }
    void cancelTimer(TimerId id) { timerQueue_->cancel(id); }

private:
    void wakeup();
    void handleWakeup();
    void doPendingFunctors();

    std::atomic<bool> looping_{false};
    std::atomic<bool> quit_{false};
    bool callingPendingFunctors_ = false;
    std::thread::id threadId_;

    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerQueue> timerQueue_;

    int wakeupFd_ = -1;
    std::unique_ptr<Channel> wakeupChannel_;

    std::mutex mutex_;
    std::vector<std::function<void()>> pendingFunctors_;

    std::vector<Channel*> activeChannels_;
};

}  // namespace net
