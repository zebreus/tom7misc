
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "netutil.h"

using namespace std;

std::string NetUtil::IPToString(uint32_t ip) {
  char buf[18] = {};
  auto [a, b, c, d] = IPToOctets(ip);
  sprintf(buf, "%d.%d.%d.%d", a, b, c, d);
  return buf;
}

std::optional<uint32_t> NetUtil::GetIPV4(const string &host,
										 string *canonical_name,
										 string *error) {
  struct addrinfo *ai_list;

  struct addrinfo ai_hints;
  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags = 0;
  ai_hints.ai_flags |= AI_ADDRCONFIG;
  ai_hints.ai_flags |= AI_CANONNAME;
  ai_hints.ai_family = AF_INET;
  ai_hints.ai_socktype = SOCK_RAW;

  int ai_return = getaddrinfo (host.c_str(), nullptr, &ai_hints, &ai_list);
  if (ai_return != 0) {
	// Can get more info from return value
	if (error != nullptr) *error = "getaddrinfo failed";
	return {};
  }

  if (ai_list == nullptr) {
	if (error != nullptr) *error = "no hosts returned";
	return {};
  }

  // First entry gets canonical name.
  if (canonical_name != nullptr) {
	if (ai_list->ai_canonname != nullptr) {
	  *canonical_name = ai_list->ai_canonname;
	} else {
	  *canonical_name = host;
	}
  }

  for (struct addrinfo *ai_ptr = ai_list;
	   ai_ptr != nullptr;
	   ai_ptr = ai_ptr->ai_next) {
	// IPV4 only
	if (ai_ptr->ai_family == AF_INET) {
	  ai_ptr->ai_socktype = SOCK_RAW;
	  ai_ptr->ai_protocol = IPPROTO_ICMP;

	  #if 0
	  printf("AF_INET is %d\n"
			 "sock->sa_family is %d\n"
			 "addrlen is %d\n"
			 "sock->sa_data is ",
			 AF_INET,
			 ai_ptr->ai_addr->sa_family,
			 ai_ptr->ai_addrlen);
	  for (int i = 0; i < (int)ai_ptr->ai_addrlen; i++) {
		printf("%d.", (uint8_t)ai_ptr->ai_addr->sa_data[i]);
	  }

	  printf(" = ");
	  for (int i = 0; i < (int)ai_ptr->ai_addrlen; i++) {
		printf("%c", ai_ptr->ai_addr->sa_data[i]);
	  }
	  printf("\n");
	  #endif

	  sockaddr_in *s = (sockaddr_in*)ai_ptr->ai_addr;
	  const uint8_t *bytes = (const uint8_t*)&s->sin_addr.s_addr;
	  uint32_t ip = OctetsToIP(bytes[0], bytes[1], bytes[2], bytes[3]);
	  #if 0
	  printf("sin_family %d\n"
			 "sin_port %d\n"
			 "s_addr %08x\n"
			 "IP %s\n",
			 s->sin_family,
			 s->sin_port,
			 s->sin_addr.s_addr,
			 IPToString(ip).c_str());
	  #endif

	  freeaddrinfo(ai_list);
	  return {ip};
	}
  }

  if (error != nullptr) *error = "no AF_INET in list";
  freeaddrinfo(ai_list);
  return {};
}

std::optional<int> NetUtil::MakeICMPSocket(string *error) {
  const int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

  if (fd == -1) {
	if (error != nullptr) {
	  *error = "call to socket failed";
	}
	return {};
  } else if (fd >= FD_SETSIZE) {
	if (error != nullptr) {
	  *error = "file descriptor too big for select(2)";
	}
	close (fd);
	return {};
  }

  // always timestamp
  {
	int optval = 1;
	int status = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP,
							&optval, sizeof (int));
	if (status != 0) {
	  if (error != nullptr) {
		*error = "couldn't enable timestamp";
	  }
	  close (fd);
	  return {};
	}
  }

  {
	/* Enable receiving the TOS field */
	int optval = 1;
	(void)setsockopt (fd, IPPROTO_IP, IP_RECVTOS, &optval, sizeof(int));
  }

  {
	int optval = 1;
	/* Enable receiving the TTL field */
	(void)setsockopt (fd, IPPROTO_IP, IP_RECVTTL, &optval, sizeof(int));
  }

  return {fd};
}

uint32_t NetUtil::OctetsToIP(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return ((uint32_t)a << 24) |
	((uint32_t)b << 16) |
	((uint32_t)c << 8) |
	(uint32_t)d;
}

std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
NetUtil::IPToOctets(uint32_t ip) {
  return make_tuple((uint8_t) ((ip >> 24) & 0xFF),
					(uint8_t) ((ip >> 16) & 0xFF),
					(uint8_t) ((ip >> 8) & 0xFF),
					(uint8_t) (ip & 0xFF));
}

sockaddr_in NetUtil::IPToSockaddr(uint32_t ip, uint16_t port) {
  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof (sockaddr_in));
  saddr.sin_family = AF_INET;
  saddr.sin_port = port;
  auto [a, b, c, d] = IPToOctets(ip);
  uint8_t *bytes = (uint8_t*)&saddr.sin_addr.s_addr;
  bytes[0] = a;
  bytes[1] = b;
  bytes[2] = c;
  bytes[3] = d;
  return saddr;
}

// from liboping
uint16_t NetUtil::ICMPChecksum(uint8_t *buf, size_t len) {
  uint32_t sum = 0;
  uint16_t ret = 0;

  uint16_t *ptr;
  for (ptr = (uint16_t *) buf; len > 1; ptr++, len -= 2)
	sum += *ptr;

  // according to RFC 792, odd lengths should be padded with zero;
  // this assumes the buffer has a zero after it? why would it?
  if (len == 1) {
	*(char *) &ret = *(char *) ptr;
	sum += ret;
  }

  /* Do this twice to get all possible carries.. */
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum = (sum >> 16) + (sum & 0xFFFF);

  ret = ~sum;

  return ret;
}
