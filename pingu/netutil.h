
#ifndef _PINGU_NETUTIL_H
#define _PINGU_NETUTIL_H

#include <optional>
#include <string>
#include <cstdint>


struct NetUtil {

  // Returns the IP address (v4) as a network-order uint32 (e.g. 128.2.1.2 is
  // 0x80020102). If canonical_name is non-null, also fills in the
  // canonical name for the host (or reuse hostname). If error is non-null,
  // populates this with additional info on failure.
  static std::optional<uint32_t> GetIPV4(const std::string &hostname,
										 std::string *canonical_name = nullptr,
										 std::string *error = nullptr);

  static std::string IPToString(uint32_t ip);

  // Make a raw IPV4 socket for sending ICMP messages. Socket options are
  // set for receiving the timestamp, tos, and ttl.
  static std::optional<int> MakeICMPSocket(std::string *error = nullptr);

};

#endif
