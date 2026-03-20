
// This is designed for cc-lib, but still experimental.

#ifndef _CC_LIB_NET_H
#define _CC_LIB_NET_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Simple wrapper for sockets on both win32 and posix.
// On windows, link with -lws2_32

struct Net {

  // Essential on windows.
  static void Init();
  static void Shutdown();

  struct Address;
  struct Socket;

  // Returns an empty vector upon failure.
  static std::vector<Address> Resolve(std::string_view host, uint16_t port);

  // Connect to the address synchronously. The returned socket will have
  // !IsValid() if connection failed.
  static Socket Connect(const Address &addr);
  // Close the socket.
  static void Close(Socket *sock);

  // Send all the data, blocking until complete.
  // Returns false if the connection was closed during sending.
  static bool SendAll(Socket *sock, std::span<const uint8_t> bytes);
  static bool SendAll(Socket *sock, std::string_view str);

  // Read into the non-empty buffer. Blocks until some data is read, and
  // returns the number of bytes read. Returns 0 on graceful end-of-stream.
  // Returns -1 if something went wrong.
  static int64_t Recv(Socket *sock, std::span<uint8_t> buffer);

  // Read until the socket is closed, and append it all to the vector.
  // Returns false if the connection fails (non-gracefully) but
  // data may still be written to out.
  static bool RecvAll(Socket *sock, std::vector<uint8_t> *out);
  static bool RecvAll(Socket *sock, std::string *out);

  // Value semantics. Keep in mind that an address that
  // comes from DNS resolution can get stale, though.
  struct Address {
    // e.g. "128.2.1.2:80", but for human consumption.
    std::string ToString() const;

   private:
    friend struct Net;
    // Use factory functions.
    Address() = default;
    int family = 0;
    int socktype = 0;
    int protocol = 0;
    uint32_t addrlen = 0;
    alignas(8) uint8_t buffer[128] = {};
  };

  struct Socket {
    operator bool() const { return IsValid(); }
    bool IsValid() const { return fd != -1; }

   private:
    friend struct Net;
    Socket() = default;
    int64_t fd = -1;
  };

 private:
  // All static.
  Net() = delete;
  struct Impl;
};


#endif
