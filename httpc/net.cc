
#include "net.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <minwindef.h>
#else

// Posix.
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>

using SOCKET = int;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close

#endif

#include "base/logging.h"

using Address = Net::Address;

// This exists to enjoy Net's friendship with the wrappers, so that
// we can make utilities that work with their private methods.
struct Net::Impl {
  static const struct sockaddr* SockAddr(const Address &addr) {
    return reinterpret_cast<const struct sockaddr*>(addr.buffer);
  }

};

void Net::Init() {
  // We use some opaque buffers in the header to avoid having all the
  // includes and platform-specific stuff.
  static_assert(sizeof(sockaddr_storage) <= sizeof (Address::buffer),
                "sockaddr_storage is too large!");

  // It is (unsigned) -1 on win32.
  static_assert(Socket().fd == INVALID_SOCKET);

  #ifdef _WIN32
  WSADATA wsaData;
  assert(WSAStartup(MAKEWORD(2, 2), &wsaData) == 0 &&
         "initializing network");
  #endif
}

void Net::Shutdown() {
  #ifdef _WIN32
  WSACleanup();
  #endif
}

std::string Net::Address::ToString() const {
  if (addrlen == 0) {
    return "(uninitialized)";
  }

  // Buffers to hold the resulting strings
  char host[NI_MAXHOST];
  char serv[NI_MAXSERV];

  if (0 != getnameinfo(Net::Impl::SockAddr(*this), addrlen,
                       host, sizeof(host),
                       serv, sizeof(serv),
                       // Don't try to reverse DNS!
                       NI_NUMERICHOST | NI_NUMERICSERV)) {
    return "(invalid)";
  }

  if (family == AF_INET6) {
    return std::format("[{}]:{}", host, serv);
  }

  // Standard IPv4 format: "128.2.1.2:80"
  return std::format("{}:{}", host, serv);
}

std::vector<Address> Net::Resolve(std::string_view hostname, uint16_t port) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  // IPV4 or 6 ok.
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (hostname.empty() || hostname.size() > 255) {
    return {};
  }

  // We need to zero-terminate, but avoid allocation.
  char host_buf[256];
  std::memcpy(host_buf, hostname.data(), hostname.size());
  host_buf[hostname.size()] = '\0';

  char port_string[10];
  snprintf(port_string, 10, "%d", port);

  struct addrinfo* result = nullptr;
  if (0 != getaddrinfo(host_buf, port_string, &hints, &result)) {
    return {};
  }

  size_t num_addrs = 0;
  for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    // Some inconsistency..?
    if (ptr->ai_addrlen > sizeof(sockaddr_storage)) [[unlikely]] {
      continue;
    }

    num_addrs++;
  }

  std::vector<Address> ret(num_addrs, Address());
  size_t idx = 0;

  for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    if (ptr->ai_addrlen > sizeof(sockaddr_storage)) [[unlikely]] {
      continue;
    }

    Address &addr = ret[idx];
    addr.family   = ptr->ai_family;
    addr.socktype = ptr->ai_socktype;
    addr.protocol = ptr->ai_protocol;
    addr.addrlen  = (uint32_t)ptr->ai_addrlen;

    std::memcpy(addr.buffer, ptr->ai_addr, ptr->ai_addrlen);
    idx++;
  }

  freeaddrinfo(result);

  return ret;
}

Net::Socket Net::Connect(const Address &addr) {
  SOCKET raw_sock = socket(addr.family, addr.socktype, addr.protocol);
  if (raw_sock == INVALID_SOCKET) {
    return Socket{};
  }

  if (connect(raw_sock,
              Net::Impl::SockAddr(addr),
              (socklen_t)addr.addrlen) == SOCKET_ERROR) {

    closesocket(raw_sock);
    return Socket{};
  }

  Socket ret;
  ret.fd = raw_sock;
  return ret;
}

void Net::Close(Socket *sock) {
  CHECK(sock && sock->IsValid());

  // Cast the 64-bit int back to the native OS socket type
  closesocket((SOCKET)sock->fd);
  sock->fd = -1;
}

bool Net::SendAll(Socket *sock, std::span<const uint8_t> bytes) {
  CHECK(sock && sock->IsValid());

  #ifndef MSG_NOSIGNAL
  // Pass no flags if not available.
  #define MSG_NOSIGNAL 0
  #endif

  while (!bytes.empty()) {
    // Windows has a 32-bit size pointer, so send only 1GB at a time.
    static constexpr size_t MAX_CHUNK = 1024 * 1024 * 1024;

    int to_send = (int)std::min(bytes.size(), MAX_CHUNK);
    CHECK(to_send > 0);

    int sent = send((SOCKET)sock->fd,
                    (const char *)bytes.data(), to_send, MSG_NOSIGNAL);

    if (sent == SOCKET_ERROR || sent == 0) {
      return false;
    }

    bytes = bytes.subspan(sent);
  }

  return true;
}

bool Net::SendAll(Socket *sock, std::string_view str) {
  return SendAll(sock, std::span<const uint8_t>((const uint8_t*)str.data(),
                                                str.size()));
}


int64_t Net::Recv(Socket *sock, std::span<uint8_t> buffer) {
  CHECK(sock && sock->IsValid() && !buffer.empty());

  // 32-bit lengths on windows.
  static constexpr size_t MAX_CHUNK = 1024 * 1024 * 1024;
  int to_read = (int)std::min(buffer.size(), MAX_CHUNK);
  int received = recv((SOCKET)sock->fd, (char*)buffer.data(), to_read, 0);

  if (received == SOCKET_ERROR) {
    return -1;
  }

  // If 0, the server gracefully closed the connection.
  return received;
}


bool Net::RecvAll(Socket *sock, std::vector<uint8_t> *out) {
  CHECK(sock && sock->IsValid() && out);

  static constexpr size_t EACH_CHUNK = 16384;

  for (;;) {
    const size_t start = out->size();
    out->resize(out->size() + EACH_CHUNK);

    int64_t bytes_read = Recv(sock, std::span(out->data() + start, EACH_CHUNK));

    if (bytes_read < 0) {
      // No data written.
      out->resize(start);
      return false;
    }

    // Keep (only) the successfully read bytes.
    out->resize(start + bytes_read);

    if (bytes_read == 0) {
      // The server finished sending data and gracefully closed the stream.
      return true;
    }
  }
}

// Just as above, but appending to a string. We could use a template
// maybe, but it's a short piece of code.
bool Net::RecvAll(Socket *sock, std::string *out) {
  CHECK(sock && sock->IsValid() && out);

  static constexpr size_t EACH_CHUNK = 16384;

  for (;;) {
    const size_t start = out->size();
    out->resize(out->size() + EACH_CHUNK);

    int64_t bytes_read = Recv(sock,
                              std::span((uint8_t *)out->data() + start,
                                        EACH_CHUNK));

    if (bytes_read < 0) {
      // No data written.
      out->resize(start);
      return false;
    }

    // Keep (only) the successfully read bytes.
    out->resize(start + bytes_read);

    if (bytes_read == 0) {
      // The server finished sending data and gracefully closed the stream.
      return true;
    }
  }
}
