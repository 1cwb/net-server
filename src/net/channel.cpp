#include "net/channel.h"

#include "net/event_loop.h"

namespace net {

Channel::Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd), events_(kNoneEvent) {}

Channel::~Channel() = default;

void Channel::handleEvent() {
    // Hold a reference for the whole dispatch: callbacks below may erase the
    // owner from its container, which would otherwise free `this` mid-call.
    std::shared_ptr<void> guard;
    if (tied_) guard = tie_.lock();
    if (tied_ && !guard) return;

    if (revents_ & kErrorEvent) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & kCloseEvent) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & kReadEvent) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & kWriteEvent) {
        if (writeCallback_) writeCallback_();
    }
    revents_ = 0;
}

void Channel::update() { loop_->updateChannel(this); }

void Channel::remove() { loop_->removeChannel(this); }

void Channel::enableReading() {
    events_ |= kReadEvent;
    update();
}

void Channel::disableReading() {
    events_ &= ~kReadEvent;
    update();
}

void Channel::enableWriting() {
    events_ |= kWriteEvent;
    update();
}

void Channel::disableWriting() {
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disableAll() {
    events_ = kNoneEvent;
    update();
}

}  // namespace net
