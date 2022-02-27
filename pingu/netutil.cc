
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>

#include <optional>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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
    if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP,
                   &optval, sizeof (int)) != 0) {
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

bool NetUtil::SendPing(int fd, const PingToSend &ping) {
  const size_t datalen = ping.data.size();
  const size_t buflen = ICMP_MINLEN + datalen;
  uint8_t buf[buflen] = {};
  struct icmp *icmp4 = (struct icmp *) buf;
  icmp4->icmp_type = ICMP_ECHO;
  icmp4->icmp_id = htons(ping.id);
  icmp4->icmp_seq = htons(ping.seq);

  uint8_t *data_dest = buf + ICMP_MINLEN;
  memcpy(data_dest, ping.data.data(), datalen);

  icmp4->icmp_cksum = NetUtil::ICMPChecksum(buf, buflen);

  sockaddr_in saddr = NetUtil::IPToSockaddr(ping.ip, 0);

  const ssize_t status = sendto(fd, buf, buflen, 0,
                                (struct sockaddr *) &saddr,
                                sizeof (sockaddr_in));

  if (status < 0) {
    switch (errno) {
    case EHOSTUNREACH:
    case ENETUNREACH:
      // BSDs return EHOSTDOWN on ARP/ND failure
    case EHOSTDOWN:
      // liboping treats these as "ok" (I guess we just time out)
      return true;
    default:
      // e.g. this will fail for a send to 127.0.0.0.
      // printf("sendto failed: %s\n", NetUtil::IPToString(ping.ip).c_str());
      return false;
    }
  }

  return true;
}

std::optional<NetUtil::Ping> NetUtil::ReceivePing(int fd, int payload_size,
                                                  std::string *error) {
  // n.b. this "payload" is internal for the socket interface; the
  // argument "payload" is for the ping's contents.
  uint8_t payload_buffer[4096];
  uint8_t control_buffer[4096];
  struct iovec payload_iovec;

  memset(&payload_iovec, 0, sizeof (payload_iovec));
  payload_iovec.iov_base = payload_buffer;
  payload_iovec.iov_len = sizeof (payload_buffer);

  struct msghdr msghdr;
  memset (&msghdr, 0, sizeof (msghdr));
  /* unspecified source address */
  msghdr.msg_name = nullptr;
  msghdr.msg_namelen = 0;
  /* output buffer vector, see readv(2) */
  msghdr.msg_iov = &payload_iovec;
  msghdr.msg_iovlen = 1;
  /* output buffer for control messages */
  msghdr.msg_control = control_buffer;
  msghdr.msg_controllen = sizeof (control_buffer);
  /* flags; this is an output-only field.. */
  msghdr.msg_flags = 0;

  ssize_t payload_buffer_len = recvmsg (fd, &msghdr, /* flags = */ 0);
  if (payload_buffer_len < 0) {
    if (error != nullptr) *error = "recvfrom failed";
    return {};
  }

  Ping ping;

  /* Iterate over all auxiliary data in msghdr */

  bool got_timestamp = false;
  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msghdr);
       cmsg != nullptr;
       cmsg = CMSG_NXTHDR (&msghdr, cmsg)) {
    switch (cmsg->cmsg_level) {
    case SOL_SOCKET:
      if (cmsg->cmsg_type == SO_TIMESTAMP) {
        // printf("Got timestamp\n");
        memcpy(&ping.recvtime, CMSG_DATA (cmsg), sizeof (timeval));
        got_timestamp = true;
      }
      break;
    case IPPROTO_IP:
      // don't actually care about cmsg_type = IP_TOS or IP_TTL
      // for this application
      break;
    default:
      break;
    }
  }

  if (!got_timestamp) {
    // If no accurate timestamp, use the current time.
    if (gettimeofday(&ping.recvtime, nullptr) == -1) {
      if (error != nullptr) *error = "gettimeofday failed??";
      return {};
    }
  }

  {
    size_t buffer_len = payload_buffer_len;
    uint8_t *buffer = payload_buffer;

    if (buffer_len < sizeof (struct ip)) {
      if (error != nullptr) *error = "packet not long enough";
      return {};
    }

    struct ip *ip_hdr = (struct ip *) buffer;
    size_t ip_hdr_len = ip_hdr->ip_hl << 2;

    if (buffer_len < ip_hdr_len) {
      if (error != nullptr) *error = "packet not as long as it claims";
      return {};
    }

    buffer += ip_hdr_len;
    buffer_len -= ip_hdr_len;

    if (buffer_len < ICMP_MINLEN) {
      // printf("Not long enough for ICMP\n");
      if (error != nullptr) *error = "not big enough for icmp";
      return {};
    }

    struct icmp *icmp_hdr = (struct icmp *) buffer;
    if (icmp_hdr->icmp_type != ICMP_ECHOREPLY) {
      // 3 is dest unreachable
      // 11 is time exceeded
      // printf("ICMP not ECHOREPLY: %d\n", (int)icmp_hdr->icmp_type);
      if (error != nullptr) *error = "packet not ECHOREPLY";
      return {};
    }

    uint16_t recv_checksum = icmp_hdr->icmp_cksum;
    /* This writes to buffer. */
    icmp_hdr->icmp_cksum = 0;
    uint16_t calc_checksum = NetUtil::ICMPChecksum(buffer, buffer_len);

    if (recv_checksum != calc_checksum) {
      if (error != nullptr) {
        char buf[128] = {};
        sprintf(buf, "Wrong checksum: Got 0x%04" PRIx16 ", "
                "calculated 0x%04" PRIx16,
                recv_checksum, calc_checksum);
        *error = buf;
      }
      return {};
    }

    ping.ident = ntohs(icmp_hdr->icmp_id);
    ping.seq = ntohs(icmp_hdr->icmp_seq);

    // Skip to data.
    buffer_len -= ICMP_MINLEN;
    buffer += ICMP_MINLEN;
    if (buffer_len != (size_t)payload_size) {
      if (error != nullptr) {
        char buf[128] = {};
        sprintf(buf, "Wrong data size: Got %d want %d\n",
                (int)buffer_len, (int)payload_size);
        *error = buf;
      }
      return {};
    }

    ping.data.resize(payload_size);
    memcpy(ping.data.data(), buffer, payload_size);
  }

  return ping;
}
