
#include "net.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <minwindef.h>
#include <winerror.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else

// Posix.
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

using SOCKET = int;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close

#endif

#include "base/logging.h"

using Address = Net::Address;
using Socket = Net::Socket;

// This exists to enjoy Net's friendship with the wrappers, so that
// we can make utilities that work with their private methods.
struct Net::Impl {
  static const struct sockaddr* SockAddr(const Address &addr) {
    return reinterpret_cast<const struct sockaddr*>(addr.buffer);
  }

  // Make a raw socket non-blocking, or return false.
  static bool MakeNonBlocking(SOCKET s) {
    #ifdef _WIN32
    u_long mode = 1;
    return 0 == ioctlsocket(s, FIONBIO, &mode);
    #else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) return false;
    return 0 == fcntl(s, F_SETFL, flags | O_NONBLOCK);
    #endif
  }

  static bool LastSelectShouldRetry(int select_return) {
    if (select_return == SOCKET_ERROR) {
      #ifdef _WIN32
      // n.b. this is allegedly extremely rare in practice.
      return WSAGetLastError() == WSAEINTR;
      #else
      return errno == EINTR || errno == EAGAIN || errno == ENOMEM;
      #endif
    }

    return false;
  }

  // Was the last network syscall (e.g. send/recv) on this thread's
  // error that it "would have blocked" (or should retry because it
  // was interrupted)?
  static bool LastCallWouldHaveBlocked() {
    #ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
    #else
    return errno == EAGAIN || errno == EWOULDBLOCK;
    #endif
  }

  // Returns false if the socket is broken (e.g. closed).
  static bool BlockUntilWritable(Socket *sock) {
    DCHECK(sock->IsValid());
    for (;;) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET((SOCKET)sock->fd, &fds);
      int ret = select((int)sock->fd + 1, nullptr, &fds, nullptr, nullptr);
      if (ret > 0) return true;
      if (!LastSelectShouldRetry(ret)) {
        return false;
      }
    }
  }

  // Returns false if the socket is broken (e.g. closed).
  static bool BlockUntilReadable(Socket *sock) {
    DCHECK(sock->IsValid());
    for (;;) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET((SOCKET)sock->fd, &fds);
      int ret = select((int)sock->fd + 1, &fds, nullptr, nullptr, nullptr);
      if (ret > 0) return true;
      if (!LastSelectShouldRetry(ret)) {
        return false;
      }
    }
  }

  // Allowing std::vector, std::string.
  template <class Cont>
  static bool RecvAllTo(Socket *sock, Cont *out) {
    CHECK(sock && sock->IsValid() && out);

    static constexpr size_t EACH_CHUNK = 16384;

    for (;;) {
      const size_t start = out->size();
      // We will resize the vector back down (including when we're looping
      // because the recv would block), but this should be cheap to do
      // repeatedly because the vector has hysteresis ('reserved').
      out->resize(out->size() + EACH_CHUNK);

      auto res = Recv(sock, std::span((uint8_t*)out->data() + start,
                                      EACH_CHUNK));
      if (std::holds_alternative<Error>(res)) {
        out->resize(start);
        return false;
      } else if (std::holds_alternative<EndOfStream>(res)) {
        out->resize(start);
        return true;
      } else {
        const size_t *bytes = std::get_if<size_t>(&res);
        CHECK(bytes != nullptr);

        out->resize(start + *bytes);
        if (*bytes == 0) {
          Impl::BlockUntilReadable(sock);
        }
      }
    }

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
    return std::format("[{}]:{}", (const char*)host, (const char*)serv);
  }

  // Standard IPv4 format: "128.2.1.2:80"
  return std::format("{}:{}", (const char*)host, (const char*)serv);
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
  if (raw_sock == INVALID_SOCKET)
    return Socket{};

  if (connect(raw_sock,
              Net::Impl::SockAddr(addr),
              (socklen_t)addr.addrlen) == SOCKET_ERROR) {

    closesocket(raw_sock);
    return Socket{};
  }

  if (!Impl::MakeNonBlocking(raw_sock)) {
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

    if (sent > 0) {
      // Success, and we made progress.
      bytes = bytes.subspan(sent);
      // We optimistically try again without blocking.
      continue;
    }

    if (sent == 0 || (sent == SOCKET_ERROR &&
                      Impl::LastCallWouldHaveBlocked())) {
      Impl::BlockUntilWritable(sock);
      continue;
    } else {
      // Failed.
      return false;
    }

    LOG(FATAL) << "Unexpected return from send.";
  }

  return true;
}

bool Net::SendAll(Socket *sock, std::string_view str) {
  return SendAll(sock, std::span<const uint8_t>((const uint8_t*)str.data(),
                                                str.size()));
}


Net::RecvResult Net::Recv(Socket *sock, std::span<uint8_t> buffer) {
  // Preconditions.
  CHECK(sock && sock->IsValid() && !buffer.empty());

  // 32-bit lengths on windows.
  static constexpr size_t MAX_CHUNK = 1024 * 1024 * 1024;
  int to_read = (int)std::min(buffer.size(), MAX_CHUNK);
  int received = recv((SOCKET)sock->fd, (char*)buffer.data(), to_read, 0);

  if (received < 0) {
    if (Impl::LastCallWouldHaveBlocked()) {
      return {(size_t)0};
    }
    return {Net::Error{}};
  }

  if (received == 0)
    return {Net::EndOfStream{}};

  return {(size_t)received};
}

int64_t Net::RecvSome(Socket *sock, std::span<uint8_t> buffer) {
  // Preconditions.
  CHECK(sock && sock->IsValid() && !buffer.empty());

  for (;;) {
    auto res = Recv(sock, buffer);
    if (std::holds_alternative<Error>(res)) {
      return -1;
    } else if (std::holds_alternative<EndOfStream>(res)) {
      return 0;
    } else {
      const size_t *bytes = std::get_if<size_t>(&res);
      CHECK(bytes != nullptr);

      if (*bytes > 0)
        return *bytes;

      // Try again when indicated.
      Impl::BlockUntilReadable(sock);
      continue;
    }
  }
}


bool Net::RecvAll(Socket *sock, std::vector<uint8_t> *out) {
  return Impl::RecvAllTo(sock, out);
}

bool Net::RecvAll(Socket *sock, std::string *out) {
  return Impl::RecvAllTo(sock, out);
}

Net::ReadySockets Net::Select(std::span<const Socket> check_read,
                              std::span<const Socket> check_write,
                              std::optional<int> timeout_ms) {
  fd_set read_fds;
  fd_set write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  #ifdef _WIN32
  CHECK(check_read.size() <= FD_SETSIZE &&
        check_write.size() <= FD_SETSIZE) << "Too "
    "many sockets passed to Select.";
  #endif

  SOCKET max_fd = 0;
  bool has_valid_sockets = false;
  auto FillSet = [&](std::span<const Socket> socks, fd_set *fds) {
    for (const Socket &s : socks) {
      if (s.IsValid()) {
        SOCKET raw_fd = (SOCKET)s.fd;

        #ifndef _WIN32
        // On POSIX, fd_set is a bitmask. We must not exceed FD_SETSIZE.
        if (raw_fd >= FD_SETSIZE) {
          fprintf(stderr, "Socket fd exceeds FD_SETSIZE!\n");
          continue;
        }
        #endif

        FD_SET(raw_fd, fds);
        has_valid_sockets = true;
        max_fd = std::max(max_fd, raw_fd);
      }
    }
  };

  FillSet(check_read, &read_fds);
  FillSet(check_write, &write_fds);

  // Nothing can be ready if nothing is valid.
  // XXX maybe should wait for the timeout if we have one, though.
  if (!has_valid_sockets) {
    return ReadySockets{};
  }

  timeval tv;
  timeval *tv_ptr = nullptr;
  if (timeout_ms.has_value()) {
    tv.tv_sec = timeout_ms.value() / 1000;
    tv.tv_usec = (timeout_ms.value() % 1000) * 1000;
    tv_ptr = &tv;
  }

  // First arg is ignored on Windows, but must be max_fd + 1 on POSIX.
  int res = select((int)max_fd + 1, &read_fds, &write_fds, nullptr, tv_ptr);

  if (res <= 0) {
    // 0 means timeout, and negative means an error; either way we
    // have no sockets ready.
    return ReadySockets{};
  }

  ReadySockets result;
  for (size_t ridx = 0; ridx < check_read.size(); ridx++) {
    const Socket &s = check_read[ridx];
    if (s.IsValid() && FD_ISSET((SOCKET)s.fd, &read_fds)) {
      result.readable.push_back(ridx);
    }
  }

  for (size_t ridx = 0; ridx < check_write.size(); ridx++) {
    const Socket &s = check_write[ridx];
    if (s.IsValid() && FD_ISSET((SOCKET)s.fd, &write_fds)) {
      result.writable.push_back(ridx);
    }
  }

  return result;
}
