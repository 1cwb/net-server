#include "net/sockets.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace net {
namespace sockets {

int create_listener(std::uint16_t port, int backlog, std::string* error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error) *error = std::string("socket(): ") + std::strerror(errno);
        return -1;
    }

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    // Allows one listener per worker; the kernel load-balances accepts.
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (error) *error = std::string("bind(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, backlog) < 0) {
        if (error) *error = std::string("listen(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_nodelay(int fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

bool peer_addr(int fd, sockaddr_in* out) {
    socklen_t len = sizeof(*out);
    return ::getpeername(fd, reinterpret_cast<sockaddr*>(out), &len) == 0;
}

bool local_addr(int fd, sockaddr_in* out) {
    socklen_t len = sizeof(*out);
    return ::getsockname(fd, reinterpret_cast<sockaddr*>(out), &len) == 0;
}

std::string addr_to_string(const sockaddr_in& addr) {
    char buf[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
}

}  // namespace sockets
}  // namespace net
