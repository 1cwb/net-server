#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codec/codec.h"

namespace net {

// Minimal HTTP/1.x framing plus a lightweight request view. Parsed fields
// point into the connection buffer and live only during the callback.
class HttpCodec : public Codec {
public:
    [[nodiscard]] int decode(Buffer& in, const Emit& emit) override;
};

namespace http {

struct Request {
    std::string_view method;
    std::string_view target;
    std::string_view version;
    std::vector<std::pair<std::string_view, std::string_view>> headers;
    std::string_view body;
    bool keepAlive = true;

    std::string_view header(std::string_view name) const;
};

bool parse_request(std::string_view frame, Request& out);

std::string make_response(int status, std::string_view content_type,
                          std::string_view body, bool keep_alive);

}  // namespace http
}  // namespace net
