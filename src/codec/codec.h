#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "net/buffer.h"

namespace net {

// One complete protocol frame. `frame` points into the connection's input
// buffer and is only valid for the duration of the emit callback.
struct Message {
    std::string_view frame;
    std::uint32_t type = 0;
};

// Protocol extension point. TCP only transports bytes; a Codec turns the byte
// stream into frames (and back) so HTTP, MQTT, or a private protocol can share
// the same transport.
class Codec {
public:
    using Emit = std::function<void(const Message&)>;

    virtual ~Codec() = default;

    // Consume complete frames from `in`, calling emit() for each. Returns 0 on
    // success (possibly with no complete frame yet) or a negative error code.
    [[nodiscard]] virtual int decode(Buffer& in, const Emit& emit) = 0;

    // Encode one already-built frame. Protocols that need structured encoding
    // can override this; the default simply appends the bytes.
    virtual void encode(std::string_view frame, Buffer& out) { out.append(frame); }
};

}  // namespace net
