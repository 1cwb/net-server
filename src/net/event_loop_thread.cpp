#include "net/event_loop_thread.h"

#include "net/event_loop.h"

namespace net {

EventLoopThread::~EventLoopThread() { stop(); }

EventLoop* EventLoopThread::startLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    thread_ = std::thread([this] { threadFunc(); });
    cond_.wait(lock, [this] { return loop_.load() != nullptr; });
    return loop_.load();
}

void EventLoopThread::stop() {
    EventLoop* loop = loop_.load();
    if (loop != nullptr) loop->quit();
    if (thread_.joinable()) thread_.join();
    loop_.store(nullptr);
}

void EventLoopThread::threadFunc() {
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_.store(&loop);
        cond_.notify_one();
    }
    loop.loop();
    loop_.store(nullptr);
}

}  // namespace net
