#include "net/event_loop_thread_pool.h"

#include "net/event_loop.h"
#include "net/event_loop_thread.h"

namespace net {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
    : baseLoop_(baseLoop), numThreads_(numThreads > 0 ? numThreads : 0) {}

EventLoopThreadPool::~EventLoopThreadPool() { stop(); }

void EventLoopThreadPool::start() {
    if (started_) return;
    started_ = true;

    if (numThreads_ == 0) {
        loops_.push_back(baseLoop_);
        return;
    }

    threads_.reserve(static_cast<std::size_t>(numThreads_));
    loops_.reserve(static_cast<std::size_t>(numThreads_));
    for (int i = 0; i < numThreads_; ++i) {
        auto thread = std::make_unique<EventLoopThread>();
        loops_.push_back(thread->startLoop());
        threads_.push_back(std::move(thread));
    }
}

void EventLoopThreadPool::stop() {
    // Join workers before their loops are destroyed so callers can still use
    // them to tear down connections, then let go of the loop pointers.
    for (auto& thread : threads_) thread->stop();
    threads_.clear();
    loops_.clear();
    started_ = false;
}

EventLoop* EventLoopThreadPool::nextLoop() {
    if (!started_ || loops_.empty()) return baseLoop_;
    EventLoop* loop = loops_[next_ % loops_.size()];
    ++next_;
    return loop;
}

}  // namespace net
