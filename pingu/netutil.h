
#ifndef _PINGU_NETUTIL_H
#define _PINGU_NETUTIL_H

#include <optional>
#include <string>
#include <cstdint>
#include <tuple>
#include <vector>
#include <optional>
#include <netinet/in.h>
#include <sys/time.h>

struct NetUtil {

  // Returns the IP address (v4) as a network-order uint32 (e.g. 128.2.1.2 is
  // 0x80020102). If canonical_name is non-null, also fills in the
  // canonical name for the host (or reuse hostname). If error is non-null,
  // populates this with additional info on failure.
  static std::optional<uint32_t> GetIPV4(const std::string &hostname,
										 std::string *canonical_name = nullptr,
										 std::string *error = nullptr);

  // a.b.c.d to network-order uint32
  static uint32_t OctetsToIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
  static std::tuple<uint8_t, uint8_t, uint8_t, uint8_t> IPToOctets(uint32_t ip);

  // IP is 32 bits as above.
  static sockaddr_in IPToSockaddr(uint32_t ip, uint16_t port);

  static std::string IPToString(uint32_t ip);

  // Make a raw IPV4 socket for sending ICMP messages. Socket options are
  // set for receiving the timestamp, tos, and ttl.
  static std::optional<int> MakeICMPSocket(std::string *error = nullptr);

  // Compute the ICMP checksum (RFC 792) for the buffer. Note the
  // checksum field should be set to zero before doing this.
  static uint16_t ICMPChecksum(uint8_t *buf, size_t len);

  struct PingToSend {
	uint16_t id = 0;
	uint16_t seq = 0;
	uint32_t ip = 0;
	std::vector<uint8_t> data;
  };
  // Send a ping on the socket (configured above). Returns false for
  // some immediate failures, but such cases will also just look
  // eventually look like timeouts (no response).
  static bool SendPing(int fd, const PingToSend &ping);

  struct Ping {
	uint16_t ident = 0;
	uint16_t seq = 0;
	struct timeval recvtime;
	std::vector<uint8_t> data;
  };
  // Receive a ping (if there is one) on the socket (configured above).
  // Returns nullopt if there is no ping or if it is invalid. In these
  // cases, populates the error string if it is non-null.
  static std::optional<Ping> ReceivePing(int fd, int payload_size,
										 std::string *error = nullptr);

  
};

#endif
