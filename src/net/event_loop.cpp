#include "net/event_loop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "net/channel.h"
#include "net/epoll_poller.h"

namespace net {

EventLoop::EventLoop()
    : threadId_(std::this_thread::get_id()),
      poller_(std::make_unique<EpollPoller>()),
      timerQueue_(std::make_unique<TimerQueue>()) {
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ >= 0) {
        wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
        wakeupChannel_->setReadCallback([this] { handleWakeup(); });
        wakeupChannel_->enableReading();
    }
}

EventLoop::~EventLoop() {
    if (wakeupChannel_) {
        wakeupChannel_->disableAll();
        wakeupChannel_->remove();
        wakeupChannel_.reset();
    }
    if (wakeupFd_ >= 0) ::close(wakeupFd_);
}

void EventLoop::loop() {
    looping_ = true;

    while (!quit_) {
        activeChannels_.clear();
        poller_->poll(timerQueue_->nextTimeoutMs(), &activeChannels_);

        for (Channel* channel : activeChannels_) {
            // An earlier callback may have closed another connection in this
            // same batch; skip channels that are no longer registered.
            if (poller_->hasChannel(channel)) channel->handleEvent();
        }

        timerQueue_->handleExpired();
        doPendingFunctors();
    }

    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) wakeup();
}

void EventLoop::runInLoop(std::function<void()> cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(std::function<void()> cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    // Wake only when the loop is not the caller, or when a running functor
    // may have just added work that must be seen in this same iteration.
    if (!isInLoopThread() || callingPendingFunctors_) wakeup();
}

void EventLoop::wakeup() {
    if (wakeupFd_ < 0) return;
    const std::uint64_t one = 1;
    const ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    (void)n;  // EAGAIN means a wakeup is already pending.
}

void EventLoop::handleWakeup() {
    std::uint64_t value = 0;
    while (::read(wakeupFd_, &value, sizeof(value)) > 0) {
    }
}

void EventLoop::updateChannel(Channel* channel) {
    if (channel == nullptr) return;
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    if (channel == nullptr) return;
    poller_->removeChannel(channel);
}

void EventLoop::doPendingFunctors() {
    std::vector<std::function<void()>> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (auto& fn : functors) {
        if (fn) fn();
    }
    callingPendingFunctors_ = false;
}

}  // namespace net
