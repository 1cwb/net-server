#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codec/codec.h"

namespace net {

// MQTT 3.1.1 framing: fixed header + variable-length Remaining Length.
class MqttCodec : public Codec {
public:
    [[nodiscard]] int decode(Buffer& in, const Emit& emit) override;
};

namespace mqtt {

enum Type : std::uint8_t {
    kConnect = 1,
    kConnack = 2,
    kPublish = 3,
    kPuback = 4,
    kPubrec = 5,
    kPubrel = 6,
    kPubcomp = 7,
    kSubscribe = 8,
    kSuback = 9,
    kUnsubscribe = 10,
    kUnsuback = 11,
    kPingreq = 12,
    kPingresp = 13,
    kDisconnect = 14,
};

struct Packet {
    std::uint8_t type = 0;   // high nibble of the first byte
    std::uint8_t flags = 0;  // low nibble (QoS lives here for PUBLISH)
    std::string_view body;   // after the Remaining Length field
    std::string_view frame;  // whole packet, borrowed from the input buffer
};

struct Publish {
    std::string_view topic;
    std::uint16_t packetId = 0;
    std::uint8_t qos = 0;
    std::string_view payload;
};

struct Subscribe {
    std::uint16_t packetId = 0;
    std::vector<std::pair<std::string_view, std::uint8_t>> filters;  // filter -> qos
};

bool parse_packet(std::string_view frame, Packet& out);
bool parse_publish(const Packet& packet, Publish& out);
bool parse_subscribe(const Packet& packet, Subscribe& out);

std::string make_connack(bool sessionPresent, std::uint8_t returnCode);
std::string make_suback(std::uint16_t packetId, const std::vector<std::uint8_t>& codes);
std::string make_puback(std::uint16_t packetId);
std::string make_pingresp();
std::string make_publish(std::string_view topic, std::string_view payload,
                         std::uint8_t qos = 0, std::uint16_t packetId = 0);

}  // namespace mqtt
}  // namespace net
