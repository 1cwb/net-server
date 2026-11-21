#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <string>

namespace net {
namespace sockets {

// Returns a non-blocking, close-on-exec listening socket or -1.
int create_listener(std::uint16_t port, int backlog, std::string* error);
void set_nodelay(int fd);

bool peer_addr(int fd, sockaddr_in* out);
bool local_addr(int fd, sockaddr_in* out);
std::string addr_to_string(const sockaddr_in& addr);

}  // namespace sockets
}  // namespace net
