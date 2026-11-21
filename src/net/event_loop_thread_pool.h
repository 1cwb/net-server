#pragma once

#include <memory>
#include <vector>

namespace net {

class EventLoop;
class EventLoopThread;

// Starts N worker loops and hands them out round-robin. With N == 0 the base
// loop is used for I/O as well.
class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads);
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void start();
    void stop();
    EventLoop* nextLoop();
    int numThreads() const { return numThreads_; }

private:
    EventLoop* baseLoop_;
    const int numThreads_;
    bool started_ = false;
    std::size_t next_ = 0;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

}  // namespace net
