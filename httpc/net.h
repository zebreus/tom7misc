
// This is designed for cc-lib, but still experimental.

#ifndef _CC_LIB_NET_H
#define _CC_LIB_NET_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Simple wrapper for sockets on both win32 and posix.
// On windows, link with -lws2_32

// Sockets are always non-blocking after connecting, although there
// are convenience wrappers that will do a single send or receive in a
// blocking-like loop.

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

  struct Error {};
  struct EndOfStream {};

  // Send all the data, blocking until complete.
  // Returns false if the connection was closed during sending.
  static bool SendAll(Socket *sock, std::span<const uint8_t> bytes);
  static bool SendAll(Socket *sock, std::string_view str);
  // Send immediately without blocking. Returns the number of
  // bytes sent (from the beginning; might not be all of them)
  // or Error if something went wrong (like the connection was
  // closed by the peer).
  using SendResult = std::variant<Error, size_t>;
  static SendResult SendNow(Socket *sock, std::span<const uint8_t> bytes);
  static SendResult SendNow(Socket *sock, std::string_view str);

  // Read into the non-empty buffer. Non-blocking. Returns one of:
  //   Number of bytes read (can be zero).
  //   Graceful end-of-stream.
  //   Socket-fatal error.
  using RecvResult = std::variant<Error, EndOfStream, size_t>;
  static RecvResult Recv(Socket *sock, std::span<uint8_t> buffer);

  // Blocks until there is some data to read (or socket is closed, or error).
  // Returns the number of bytes read; this is 0 on graceful end of stream.
  // Returns -1 if something went wrong.
  static int64_t RecvSome(Socket *sock, std::span<uint8_t> buffer);

  // Read until the socket is closed, and append it all to the vector.
  // Returns false if the connection fails (non-gracefully), but
  // data may still be appended to out in any case.
  static bool RecvAll(Socket *sock, std::vector<uint8_t> *out);
  static bool RecvAll(Socket *sock, std::string *out);

  // From a modest number of sockets (up to ~64), check whether
  // they are ready to read and/or write data without blocking.
  // Waits until at least one is ready, or up to the timeout.
  struct ReadySockets {
    // Indexing into the read span.
    std::vector<int> readable;
    // And the write span.
    std::vector<int> writable;
  };
  static ReadySockets Select(std::span<const Socket> check_read,
                             std::span<const Socket> check_write,
                             // Returns immediately.
                             std::optional<int> timeout_ms = {0});

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

    // In an invalid state. Use Net functions to get
    // valid sockets.
    constexpr Socket() {}

   private:
    friend struct Net;
    int64_t fd = -1;
  };

 private:
  // All static.
  Net() = delete;
  struct Impl;
};


#endif
