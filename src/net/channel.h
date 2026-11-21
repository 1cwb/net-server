#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace net {

class EventLoop;

// A file descriptor plus the events we care about and the callbacks to run.
// Events are logical bits so the poller implementation stays replaceable.
class Channel {
public:
    using EventCallback = std::function<void()>;

    static constexpr std::uint32_t kNoneEvent = 0;
    static constexpr std::uint32_t kReadEvent = 1u << 0;
    static constexpr std::uint32_t kWriteEvent = 1u << 1;
    static constexpr std::uint32_t kErrorEvent = 1u << 2;
    static constexpr std::uint32_t kCloseEvent = 1u << 3;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void handleEvent();

    // Keep the owning object alive while callbacks run, so a callback may
    // safely drop the last external reference to it.
    void tie(const std::shared_ptr<void>& owner) {
        tie_ = owner;
        tied_ = true;
    }

    int fd() const { return fd_; }
    std::uint32_t events() const { return events_; }
    void setRevents(std::uint32_t revents) { revents_ = revents; }

    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return (events_ & kWriteEvent) != 0; }
    bool isReading() const { return (events_ & kReadEvent) != 0; }

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    void remove();

    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }

private:
    void update();

    EventLoop* loop_;
    const int fd_;
    std::uint32_t events_;
    std::uint32_t revents_ = 0;
    bool tied_ = false;
    std::weak_ptr<void> tie_;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback errorCallback_;
    EventCallback closeCallback_;
};

}  // namespace net
