#include "codec/http_codec.h"

#include <cctype>
#include <cstddef>

namespace net {
namespace {

constexpr std::size_t kNpos = std::string_view::npos;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (iequals(haystack.substr(i, needle.size()), needle)) return true;
    }
    return false;
}

bool parse_decimal(std::string_view s, std::size_t& out) {
    if (s.empty()) return false;
    std::size_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    out = value;
    return true;
}

bool parse_hex(std::string_view s, std::size_t& out) {
    if (s.empty()) return false;
    std::size_t value = 0;
    for (char c : s) {
        std::size_t digit;
        if (c >= '0' && c <= '9') digit = static_cast<std::size_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<std::size_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<std::size_t>(c - 'A' + 10);
        else return false;
        value = value * 16 + digit;
    }
    out = value;
    return true;
}

// Returns 1 and sets frameLen on a full frame, 0 when more bytes are needed,
// and -1 on malformed input.
int frame_length(std::string_view buf, std::size_t& frameLen) {
    const std::size_t headerEnd = buf.find("\r\n\r\n");
    if (headerEnd == kNpos) return 0;
    const std::size_t headerLen = headerEnd + 4;

    bool chunked = false;
    bool hasContentLength = false;
    std::size_t contentLength = 0;

    const std::size_t firstLineEnd = buf.find("\r\n");
    std::size_t pos = firstLineEnd == kNpos ? headerEnd : firstLineEnd + 2;
    while (pos < headerEnd) {
        std::size_t lineEnd = buf.find("\r\n", pos);
        if (lineEnd == kNpos || lineEnd > headerEnd) break;
        const std::string_view line = buf.substr(pos, lineEnd - pos);
        const std::size_t colon = line.find(':');
        if (colon != kNpos) {
            const std::string_view name = trim(line.substr(0, colon));
            const std::string_view value = trim(line.substr(colon + 1));
            if (iequals(name, "Content-Length")) {
                if (!parse_decimal(value, contentLength)) return -1;
                hasContentLength = true;
            } else if (iequals(name, "Transfer-Encoding") && icontains(value, "chunked")) {
                chunked = true;
            }
        }
        pos = lineEnd + 2;
    }

    if (chunked) {
        std::size_t p = headerLen;
        for (;;) {
            const std::size_t eol = buf.find("\r\n", p);
            if (eol == kNpos) return 0;
            std::string_view sizeLine = buf.substr(p, eol - p);
            const std::size_t semi = sizeLine.find(';');
            if (semi != kNpos) sizeLine = sizeLine.substr(0, semi);

            std::size_t chunkSize = 0;
            if (!parse_hex(trim(sizeLine), chunkSize)) return -1;
            p = eol + 2;

            if (chunkSize == 0) {
                if (buf.size() < p + 2) return 0;
                if (buf.compare(p, 2, "\r\n") == 0) {
                    frameLen = p + 2;
                    return 1;
                }
                const std::size_t trailers = buf.find("\r\n\r\n", p);
                if (trailers == kNpos) return 0;
                frameLen = trailers + 4;
                return 1;
            }

            if (buf.size() < p + chunkSize + 2) return 0;
            if (buf.compare(p + chunkSize, 2, "\r\n") != 0) return -1;
            p += chunkSize + 2;
        }
    }

    const std::size_t need = headerLen + (hasContentLength ? contentLength : 0);
    if (buf.size() < need) return 0;
    frameLen = need;
    return 1;
}

const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

}  // namespace

int HttpCodec::decode(Buffer& in, const Emit& emit) {
    for (;;) {
        const std::string_view buf = in.view();
        if (buf.empty()) return 0;

        std::size_t frameLen = 0;
        const int rc = frame_length(buf, frameLen);
        if (rc == 0) return 0;
        if (rc < 0) return -1;

        emit(Message{buf.substr(0, frameLen), 1});
        in.retrieve(frameLen);
    }
}

namespace http {

std::string_view Request::header(std::string_view name) const {
    for (const auto& entry : headers) {
        if (iequals(entry.first, name)) return entry.second;
    }
    return {};
}

bool parse_request(std::string_view frame, Request& out) {
    out = Request{};

    const std::size_t headerEnd = frame.find("\r\n\r\n");
    if (headerEnd == kNpos) return false;

    const std::string_view head = frame.substr(0, headerEnd);
    const std::size_t firstLineEnd = head.find("\r\n");
    const std::string_view requestLine =
        firstLineEnd == kNpos ? head : head.substr(0, firstLineEnd);

    const std::size_t sp1 = requestLine.find(' ');
    if (sp1 == kNpos) return false;
    const std::size_t sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == kNpos) return false;

    out.method = requestLine.substr(0, sp1);
    out.target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    out.version = requestLine.substr(sp2 + 1);

    std::size_t pos = firstLineEnd == kNpos ? head.size() : firstLineEnd + 2;
    while (pos < head.size()) {
        std::size_t eol = head.find("\r\n", pos);
        if (eol == kNpos) eol = head.size();
        const std::string_view line = head.substr(pos, eol - pos);
        const std::size_t colon = line.find(':');
        if (colon != kNpos) {
            out.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
        }
        pos = eol + 2;
    }

    out.body = frame.substr(headerEnd + 4);

    const std::string_view connection = out.header("Connection");
    if (!connection.empty()) {
        if (iequals(connection, "close")) {
            out.keepAlive = false;
        } else if (iequals(connection, "keep-alive")) {
            out.keepAlive = true;
        } else {
            out.keepAlive = out.version == "HTTP/1.1";
        }
    } else {
        out.keepAlive = out.version == "HTTP/1.1";
    }

    return !out.method.empty() && !out.target.empty();
}

std::string make_response(int status, std::string_view content_type,
                          std::string_view body, bool keep_alive) {
    std::string out;
    out.reserve(160 + body.size());
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += status_text(status);
    out += "\r\n";
    if (!content_type.empty()) {
        out += "Content-Type: ";
        out.append(content_type.data(), content_type.size());
        out += "\r\n";
    }
    out += "Content-Length: ";
    out += std::to_string(body.size());
    out += "\r\n";
    out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    out += "\r\n";
    out.append(body.data(), body.size());
    return out;
}

}  // namespace http
}  // namespace net
