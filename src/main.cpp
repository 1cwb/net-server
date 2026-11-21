#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "codec/http_codec.h"
#include "codec/mqtt_codec.h"
#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "runtime.h"

namespace {

constexpr std::uint16_t kDefaultTcpPort = 9000;
constexpr std::uint16_t kDefaultHttpPort = 8080;
constexpr std::uint16_t kDefaultMqttPort = 1883;
constexpr int kDefaultThreads = 1;

std::uint16_t read_env_u16(const char* name, std::uint16_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return fallback;
    return static_cast<std::uint16_t>(std::strtoul(value, nullptr, 10));
}

int read_env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return fallback;
    return std::atoi(value);
}

void usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n"
        "  --tcp-port <port>    raw TCP echo port (default %u)\n"
        "  --http-port <port>   HTTP port (default %u)\n"
        "  --mqtt-port <port>   MQTT port (default %u)\n"
        "  --threads <n>        worker loops per server (default %d)\n"
        "  -h, --help           show this help\n",
        prog, static_cast<unsigned>(kDefaultTcpPort),
        static_cast<unsigned>(kDefaultHttpPort),
        static_cast<unsigned>(kDefaultMqttPort), kDefaultThreads);
}

// Raw TCP: echo every complete read back to the peer.
void handle_echo(const std::shared_ptr<net::TcpConnection>& conn, const net::Message& msg) {
    conn->send(msg.frame);
}

// HTTP: a few endpoints to exercise keep-alive, bodies and streaming.
void handle_http(const std::shared_ptr<net::TcpConnection>& conn, const net::Message& msg) {
    net::http::Request req;
    if (!net::http::parse_request(msg.frame, req)) {
        conn->send(net::http::make_response(400, "text/plain; charset=utf-8",
                                            "bad request\n", false));
        conn->shutdown();
        return;
    }

    const bool keepAlive = req.keepAlive;

    if (req.method == "GET" && req.target == "/") {
        static constexpr std::string_view kIndex =
            "app_center demo\n"
            "  GET  /health\n"
            "  GET  /time\n"
            "  POST /echo   body is echoed back\n";
        conn->send(net::http::make_response(200, "text/plain; charset=utf-8", kIndex,
                                            keepAlive));
    } else if (req.method == "GET" && req.target == "/health") {
        conn->send(net::http::make_response(200, "text/plain; charset=utf-8", "ok\n",
                                            keepAlive));
    } else if (req.method == "GET" && req.target == "/time") {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S\n", &tm);
        conn->send(net::http::make_response(200, "text/plain; charset=utf-8", buf,
                                            keepAlive));
    } else if (req.method == "POST" && req.target == "/echo") {
        conn->send(net::http::make_response(200, "application/octet-stream", req.body,
                                            keepAlive));
    } else {
        conn->send(net::http::make_response(404, "text/plain; charset=utf-8",
                                            "not found\n", keepAlive));
    }

    if (!keepAlive) conn->shutdown();
}

// A deliberately small MQTT broker core: connect, subscribe, publish (QoS 0/1)
// and ping. It is enough to prove the codec and the shared transport.
class MqttHub {
public:
    void on_connection(const std::shared_ptr<net::TcpConnection>& conn) {
        if (conn->connected()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = topics_.begin(); it != topics_.end();) {
            it->second.erase(conn.get());
            if (it->second.empty()) {
                it = topics_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void on_message(const std::shared_ptr<net::TcpConnection>& conn, const net::Message& msg) {
        net::mqtt::Packet packet;
        if (!net::mqtt::parse_packet(msg.frame, packet)) return;

        switch (packet.type) {
            case net::mqtt::kConnect:
                conn->send(net::mqtt::make_connack(false, 0));
                break;
            case net::mqtt::kPingreq:
                conn->send(net::mqtt::make_pingresp());
                break;
            case net::mqtt::kDisconnect:
                conn->shutdown();
                break;
            case net::mqtt::kSubscribe:
                handle_subscribe(conn, packet);
                break;
            case net::mqtt::kPublish:
                handle_publish(conn, packet);
                break;
            default:
                break;
        }
    }

private:
    void handle_subscribe(const std::shared_ptr<net::TcpConnection>& conn,
                          const net::mqtt::Packet& packet) {
        net::mqtt::Subscribe sub;
        if (!net::mqtt::parse_subscribe(packet, sub)) return;

        std::vector<std::uint8_t> granted;
        granted.reserve(sub.filters.size());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& filter : sub.filters) {
                const std::uint8_t qos = filter.second;
                if (qos > 1) {
                    granted.push_back(0x80);  // failure: unsupported QoS
                    continue;
                }
                topics_[std::string(filter.first)][conn.get()] = conn;
                granted.push_back(qos);
            }
        }

        conn->send(net::mqtt::make_suback(sub.packetId, granted));
    }

    void handle_publish(const std::shared_ptr<net::TcpConnection>& conn,
                        const net::mqtt::Packet& packet) {
        net::mqtt::Publish pub;
        if (!net::mqtt::parse_publish(packet, pub)) return;

        const std::string topic(pub.topic);
        const std::string payload(pub.payload);

        std::vector<std::shared_ptr<net::TcpConnection>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = topics_.find(topic);
            if (it != topics_.end()) {
                for (auto entry = it->second.begin(); entry != it->second.end();) {
                    auto target = entry->second.lock();
                    if (target) {
                        targets.push_back(std::move(target));
                        ++entry;
                    } else {
                        entry = it->second.erase(entry);
                    }
                }
            }
        }

        if (pub.qos > 0) conn->send(net::mqtt::make_puback(pub.packetId));

        const std::string forwarded = net::mqtt::make_publish(topic, payload);
        for (auto& target : targets) target->send(forwarded);
    }

    std::mutex mutex_;
    // topic -> (connection pointer -> weak handle) keeps lookup O(log n) and
    // lets a closed connection be pruned without scan-by-value.
    std::map<std::string, std::map<net::TcpConnection*, std::weak_ptr<net::TcpConnection>>>
        topics_;
};

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t tcpPort = read_env_u16("APP_CENTER_TCP_PORT", kDefaultTcpPort);
    std::uint16_t httpPort = read_env_u16("APP_CENTER_HTTP_PORT", kDefaultHttpPort);
    std::uint16_t mqttPort = read_env_u16("APP_CENTER_MQTT_PORT", kDefaultMqttPort);
    int threads = read_env_int("APP_CENTER_THREADS", kDefaultThreads);

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value_of = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", opt);
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        if (arg == "--tcp-port") {
            tcpPort = static_cast<std::uint16_t>(std::strtoul(value_of("--tcp-port"), nullptr, 10));
        } else if (arg == "--http-port") {
            httpPort =
                static_cast<std::uint16_t>(std::strtoul(value_of("--http-port"), nullptr, 10));
        } else if (arg == "--mqtt-port") {
            mqttPort =
                static_cast<std::uint16_t>(std::strtoul(value_of("--mqtt-port"), nullptr, 10));
        } else if (arg == "--threads") {
            threads = std::atoi(value_of("--threads"));
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (threads < 0) threads = 0;

    app_center::install_signal_handlers();

    net::EventLoop baseLoop;
    {
        MqttHub hub;

        net::TcpServer echoServer(&baseLoop, tcpPort, threads, "echo");
        net::TcpServer httpServer(&baseLoop, httpPort, threads, "http");
        net::TcpServer mqttServer(&baseLoop, mqttPort, threads, "mqtt");

        if (!echoServer.ok()) {
            std::fprintf(stderr, "[app_center] tcp setup failed: %s\n",
                         echoServer.error().c_str());
            return EXIT_FAILURE;
        }
        if (!httpServer.ok()) {
            std::fprintf(stderr, "[app_center] http setup failed: %s\n",
                         httpServer.error().c_str());
            return EXIT_FAILURE;
        }
        if (!mqttServer.ok()) {
            std::fprintf(stderr, "[app_center] mqtt setup failed: %s\n",
                         mqttServer.error().c_str());
            return EXIT_FAILURE;
        }

        echoServer.setMessageCallback(handle_echo);

        httpServer.setCodecFactory([] { return std::make_unique<net::HttpCodec>(); });
        httpServer.setMessageCallback(handle_http);

        mqttServer.setCodecFactory([] { return std::make_unique<net::MqttCodec>(); });
        mqttServer.setConnectionCallback(
            [&hub](const std::shared_ptr<net::TcpConnection>& conn) {
                hub.on_connection(conn);
            });
        mqttServer.setMessageCallback(
            [&hub](const std::shared_ptr<net::TcpConnection>& conn, const net::Message& msg) {
                hub.on_message(conn, msg);
            });

        echoServer.start();
        httpServer.start();
        mqttServer.start();

        std::printf(
            "[app_center] tcp :%u  http :%u  mqtt :%u  workers/server %d\n",
            static_cast<unsigned>(tcpPort), static_cast<unsigned>(httpPort),
            static_cast<unsigned>(mqttPort), threads);

        // Poll the signal flag from the loop thread instead of touching the
        // loop from the handler.
        baseLoop.runEvery(200, [&baseLoop] {
            if (!app_center::g_running) baseLoop.quit();
        });

        baseLoop.loop();
    }

    std::printf("[app_center] stopped\n");
    return EXIT_SUCCESS;
}
