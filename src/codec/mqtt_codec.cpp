#include "codec/mqtt_codec.h"

#include <cstddef>

namespace net {
namespace {

constexpr std::size_t kNpos = std::string_view::npos;
constexpr std::size_t kMaxRemainingLength = 16 * 1024 * 1024;  // guard against abuse

// Decodes MQTT's base-128 variable integer. Returns 1 on success, 0 when more
// bytes are needed, -1 when the encoding is invalid.
int decode_varint(std::string_view buf, std::size_t offset, std::size_t& value,
                  std::size_t& bytes) {
    value = 0;
    bytes = 0;
    std::size_t multiplier = 1;

    for (int i = 0; i < 4; ++i) {
        if (offset + static_cast<std::size_t>(i) >= buf.size()) return 0;
        const std::uint8_t b = static_cast<std::uint8_t>(buf[offset + static_cast<std::size_t>(i)]);
        value += static_cast<std::size_t>(b & 0x7F) * multiplier;
        ++bytes;
        if ((b & 0x80) == 0) return 1;
        multiplier *= 128;
    }
    return -1;
}

int frame_length(std::string_view buf, std::size_t& frameLen) {
    if (buf.size() < 2) return 0;

    const std::uint8_t type = static_cast<std::uint8_t>(buf[0]) >> 4;
    if (type == 0 || type == 15) return -1;

    std::size_t remaining = 0;
    std::size_t lenBytes = 0;
    const int rc = decode_varint(buf, 1, remaining, lenBytes);
    if (rc <= 0) return rc;
    if (remaining > kMaxRemainingLength) return -1;

    const std::size_t total = 1 + lenBytes + remaining;
    if (buf.size() < total) return 0;
    frameLen = total;
    return 1;
}

void append_varint(std::string& out, std::size_t value) {
    do {
        std::uint8_t b = static_cast<std::uint8_t>(value % 128);
        value /= 128;
        if (value > 0) b |= 0x80;
        out.push_back(static_cast<char>(b));
    } while (value > 0);
}

void append_u16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

std::uint16_t read_u16(std::string_view s, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint8_t>(s[offset]) << 8) |
                                      static_cast<std::uint8_t>(s[offset + 1]));
}

}  // namespace

int MqttCodec::decode(Buffer& in, const Emit& emit) {
    for (;;) {
        const std::string_view buf = in.view();
        if (buf.empty()) return 0;

        std::size_t frameLen = 0;
        const int rc = frame_length(buf, frameLen);
        if (rc == 0) return 0;
        if (rc < 0) return -1;

        const auto type = static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[0]) >> 4);
        emit(Message{buf.substr(0, frameLen), type});
        in.retrieve(frameLen);
    }
}

namespace mqtt {

bool parse_packet(std::string_view frame, Packet& out) {
    if (frame.size() < 2) return false;

    std::size_t remaining = 0;
    std::size_t lenBytes = 0;
    if (decode_varint(frame, 1, remaining, lenBytes) != 1) return false;

    const std::size_t bodyStart = 1 + lenBytes;
    if (frame.size() < bodyStart + remaining) return false;

    out.type = static_cast<std::uint8_t>(frame[0]) >> 4;
    out.flags = static_cast<std::uint8_t>(frame[0]) & 0x0F;
    out.body = frame.substr(bodyStart, remaining);
    out.frame = frame;
    return true;
}

bool parse_publish(const Packet& packet, Publish& out) {
    if (packet.type != kPublish || packet.body.size() < 2) return false;

    const std::uint16_t topicLen = read_u16(packet.body, 0);
    if (packet.body.size() < 2 + static_cast<std::size_t>(topicLen)) return false;

    out.topic = packet.body.substr(2, topicLen);
    out.qos = static_cast<std::uint8_t>((packet.flags >> 1) & 0x03);
    out.packetId = 0;

    std::size_t offset = 2 + static_cast<std::size_t>(topicLen);
    if (out.qos > 0) {
        if (packet.body.size() < offset + 2) return false;
        out.packetId = read_u16(packet.body, offset);
        offset += 2;
    }
    out.payload = packet.body.substr(offset);
    return true;
}

bool parse_subscribe(const Packet& packet, Subscribe& out) {
    if (packet.type != kSubscribe || packet.body.size() < 2) return false;

    out.packetId = read_u16(packet.body, 0);
    out.filters.clear();

    std::size_t offset = 2;
    while (offset < packet.body.size()) {
        if (packet.body.size() < offset + 2) return false;
        const std::uint16_t topicLen = read_u16(packet.body, offset);
        offset += 2;
        if (packet.body.size() < offset + static_cast<std::size_t>(topicLen) + 1) return false;

        const std::string_view filter = packet.body.substr(offset, topicLen);
        offset += topicLen;
        const auto qos = static_cast<std::uint8_t>(packet.body[offset]);
        offset += 1;
        out.filters.emplace_back(filter, qos);
    }
    return !out.filters.empty();
}

std::string make_connack(bool sessionPresent, std::uint8_t returnCode) {
    std::string out;
    out.reserve(4);
    out.push_back(static_cast<char>(kConnack << 4));
    out.push_back(static_cast<char>(2));
    out.push_back(static_cast<char>(sessionPresent ? 0x01 : 0x00));
    out.push_back(static_cast<char>(returnCode));
    return out;
}

std::string make_suback(std::uint16_t packetId, const std::vector<std::uint8_t>& codes) {
    std::string out;
    out.reserve(4 + codes.size());
    out.push_back(static_cast<char>(kSuback << 4));
    append_varint(out, 2 + codes.size());
    append_u16(out, packetId);
    for (std::uint8_t code : codes) out.push_back(static_cast<char>(code));
    return out;
}

std::string make_puback(std::uint16_t packetId) {
    std::string out;
    out.reserve(4);
    out.push_back(static_cast<char>(kPuback << 4));
    out.push_back(static_cast<char>(2));
    append_u16(out, packetId);
    return out;
}

std::string make_pingresp() {
    std::string out;
    out.push_back(static_cast<char>(kPingresp << 4));
    out.push_back(static_cast<char>(0));
    return out;
}

std::string make_publish(std::string_view topic, std::string_view payload,
                         std::uint8_t qos, std::uint16_t packetId) {
    std::string body;
    body.reserve(2 + topic.size() + (qos > 0 ? 2 : 0) + payload.size());
    append_u16(body, static_cast<std::uint16_t>(topic.size()));
    body.append(topic.data(), topic.size());
    if (qos > 0) append_u16(body, packetId);
    body.append(payload.data(), payload.size());

    std::string out;
    out.reserve(1 + 4 + body.size());
    out.push_back(static_cast<char>((kPublish << 4) | ((qos & 0x03) << 1)));
    append_varint(out, body.size());
    out += body;
    return out;
}

}  // namespace mqtt
}  // namespace net
