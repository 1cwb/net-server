#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace net {

// Byte buffer shared by the reader and writer paths of a connection.
// Layout: [ prepend spare | readable | writable ]
class Buffer {
public:
    static constexpr std::size_t kCheapPrepend = 8;
    static constexpr std::size_t kInitialSize = 1024;

    explicit Buffer(std::size_t initial = kInitialSize)
        : data_(kCheapPrepend + initial), reader_(kCheapPrepend), writer_(kCheapPrepend) {}

    const char* peek() const { return data_.data() + reader_; }
    char* beginWrite() { return data_.data() + writer_; }

    std::size_t readableBytes() const { return writer_ - reader_; }
    std::size_t writableBytes() const { return data_.size() - writer_; }
    std::size_t prependableBytes() const { return reader_; }

    std::string_view view() const { return {peek(), readableBytes()}; }

    void retrieve(std::size_t len) {
        if (len < readableBytes()) {
            reader_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveAll() { reader_ = writer_ = kCheapPrepend; }

    std::string retrieveAllAsString() {
        std::string out(peek(), readableBytes());
        retrieveAll();
        return out;
    }

    void append(const char* data, std::size_t len) {
        ensureWritable(len);
        std::memcpy(beginWrite(), data, len);
        writer_ += len;
    }

    void append(std::string_view s) { append(s.data(), s.size()); }

    void hasWritten(std::size_t len) { writer_ += len; }

    void prepend(const void* data, std::size_t len) {
        reader_ -= len;
        std::memcpy(data_.data() + reader_, data, len);
    }

    // Make room without invalidating the readable prefix unnecessarily.
    void ensureWritable(std::size_t len) {
        if (writableBytes() >= len) return;
        if (prependableBytes() + writableBytes() >= len + kCheapPrepend) {
            const std::size_t readable = readableBytes();
            std::memmove(data_.data() + kCheapPrepend, peek(), readable);
            reader_ = kCheapPrepend;
            writer_ = reader_ + readable;
        } else {
            data_.resize(writer_ + len);
        }
    }

private:
    std::vector<char> data_;
    std::size_t reader_;
    std::size_t writer_;
};

}  // namespace net
