#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace net {

class EventLoop;

// Owns a thread whose only job is to run one EventLoop.
class EventLoopThread {
public:
    EventLoopThread() = default;
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    // Starts the thread and blocks until the loop exists.
    EventLoop* startLoop();
    void stop();

private:
    void threadFunc();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<EventLoop*> loop_{nullptr};
};

}  // namespace net
