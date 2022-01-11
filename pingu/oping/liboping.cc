/**
 * Object oriented C module to send ICMP and ICMPv6 `echo's.
 * Copyright (C) 2006-2017  Florian octo Forster <ff at octo.it>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>

#include <stdint.h>
#include <unistd.h>

#include <vector>

#include <fcntl.h>
#include <sys/types.h>

#include <sys/stat.h>

#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include "base/logging.h"

#include "oping.h"
#include "../netutil.h"

#if WITH_DEBUG
# define dprintf(...) printf ("%s[%4i]: %-20s: ", __FILE__, __LINE__, __FUNCTION__); printf (__VA_ARGS__)
#else
# define dprintf(...) /**/
#endif

#define PING_ERRMSG_LEN 256
#define PING_OUTERRMSG_LEN (256 + 64)

struct pinghost {
  pinghost() {
	memset (&sockaddr, 0, sizeof (sockaddr_storage));
	memset (&tv, 0, sizeof (timeval));
  }

  /* username: name (of the host) passed in by the user */
  std::string username;
  /* hostname: name returned by the reverse lookup */
  std::string hostname;
  int ident = 0;
  int sequence = 0;
  double latency = 0.0;
  uint32_t dropped = 0;
  int recv_ttl = 0;
  uint8_t recv_qos = 0;
  std::vector<uint8_t> data;

  void *context = nullptr;

  socklen_t addrlen = 0;
  sockaddr_storage sockaddr;
  timeval tv;
};

struct pingobj {
  double timeout = 0.0;
  std::vector<uint8_t> data;

  int fd4 = -1;

  char errmsg[PING_OUTERRMSG_LEN] = {};

  // flat vector of all of the stored hosts; no longer
  // bothering with hashing
  std::vector<pinghost *> table;
};

static char *sstrerror (int errnum, char *buf, size_t buflen) {
  snprintf(buf, buflen, "Error %d", errnum);
  buf[buflen - 1] = 0;
  return buf;
}

static void ping_set_error (pingobj *obj, const char *function,
							const char *message) {
  snprintf (obj->errmsg, sizeof (obj->errmsg),
			"%s: %s", function, message);
  obj->errmsg[sizeof (obj->errmsg) - 1] = 0;
}

static void ping_set_errno (pingobj *obj, int error_number) {
  sstrerror (error_number, obj->errmsg, sizeof (obj->errmsg));
}

static int ping_timeval_add (struct timeval *tv1, struct timeval *tv2,
							 struct timeval *res) {
  res->tv_sec = tv1->tv_sec  + tv2->tv_sec;
  res->tv_usec = tv1->tv_usec + tv2->tv_usec;

  while (res->tv_usec > 1000000) {
	res->tv_usec -= 1000000;
	res->tv_sec++;
  }

  return 0;
}

static int ping_timeval_sub (struct timeval *tv1, struct timeval *tv2,
							 struct timeval *res) {
  if ((tv1->tv_sec < tv2->tv_sec)
	  || ((tv1->tv_sec == tv2->tv_sec)
		  && (tv1->tv_usec < tv2->tv_usec)))
	return -1;

  res->tv_sec = tv1->tv_sec  - tv2->tv_sec;
  res->tv_usec = tv1->tv_usec - tv2->tv_usec;

  assert ((res->tv_sec > 0) || ((res->tv_sec == 0) && (res->tv_usec >= 0)));

  while (res->tv_usec < 0) {
	res->tv_usec += 1000000;
	res->tv_sec--;
  }

  return 0;
}

static pinghost *ping_receive_ipv4 (pingobj *obj, uint8_t *buffer,
									size_t buffer_len) {
  if (buffer_len < sizeof (struct ip))
	return nullptr;

  struct ip *ip_hdr = (struct ip *) buffer;
  size_t ip_hdr_len = ip_hdr->ip_hl << 2;

  if (buffer_len < ip_hdr_len)
	return nullptr;

  buffer += ip_hdr_len;
  buffer_len -= ip_hdr_len;

  if (buffer_len < ICMP_MINLEN)
	return nullptr;

  struct icmp *icmp_hdr = (struct icmp *) buffer;
  if (icmp_hdr->icmp_type != ICMP_ECHOREPLY) {
	dprintf ("Unexpected ICMP type: %" PRIu8 "\n", icmp_hdr->icmp_type);
	return nullptr;
  }

  uint16_t recv_checksum = icmp_hdr->icmp_cksum;
  /* This writes to buffer. */
  icmp_hdr->icmp_cksum = 0;
  uint16_t calc_checksum = NetUtil::ICMPChecksum(buffer, buffer_len);

  if (recv_checksum != calc_checksum) {
	dprintf ("Checksum missmatch: Got 0x%04" PRIx16 ", "
			 "calculated 0x%04" PRIx16 "\n",
			 recv_checksum, calc_checksum);
	return nullptr;
  }

  uint16_t ident = ntohs (icmp_hdr->icmp_id);
  uint16_t seq = ntohs (icmp_hdr->icmp_seq);

  // try to find matching ping
  for (pinghost *host : obj->table) {
	dprintf ("hostname = %s, ident = 0x%04x, seq = %i\n",
			 host->hostname, host->ident, ((host->sequence - 1) & 0xFFFF));

	if (!timerisset (&host->tv))
	  continue;

	if (host->ident != ident)
	  continue;

	// sequence has been incremented
	if (((host->sequence - 1) & 0xFFFF) != seq)
	  continue;

	dprintf ("Match found: hostname = %s, ident = 0x%04" PRIx16 ", "
			 "seq = %" PRIu16 "\n",
			 host->hostname, ident, seq);

	host->recv_ttl = (int)     ip_hdr->ip_ttl;
	host->recv_qos = (uint8_t) ip_hdr->ip_tos;
	return host;
  }

  dprintf ("No match found for ident = 0x%04" PRIx16
		   ", seq = %" PRIu16 "\n",
		   ident, seq);
  return nullptr;
}


static int ping_receive_one (pingobj *obj, struct timeval *now) {
  const int fd = obj->fd4;
  struct timeval diff, pkt_now = *now;

  uint8_t payload_buffer[4096];
  uint8_t control_buffer[4096];
  struct iovec payload_iovec;

  memset (&payload_iovec, 0, sizeof (payload_iovec));
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
  /* flags; this is an output only field.. */
  msghdr.msg_flags = 0;

  ssize_t payload_buffer_len = recvmsg (fd, &msghdr, /* flags = */ 0);
  if (payload_buffer_len < 0) {
#if WITH_DEBUG
	char errbuf[PING_ERRMSG_LEN];
	dprintf ("recvfrom: %s\n",
			 sstrerror (errno, errbuf, sizeof (errbuf)));
#endif
	return -1;
  }
  dprintf ("Read %zi bytes from fd = %i\n", payload_buffer_len, fd);

  /* Iterate over all auxiliary data in msghdr */
  int recv_ttl = -1;
  uint8_t recv_qos = 0;
  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR (&msghdr);
	   cmsg != nullptr;
	   cmsg = CMSG_NXTHDR (&msghdr, cmsg)) {
	if (cmsg->cmsg_level == SOL_SOCKET) {
	  if (cmsg->cmsg_type == SO_TIMESTAMP)
		memcpy (&pkt_now, CMSG_DATA (cmsg), sizeof (pkt_now));
	} else {
	  if (cmsg->cmsg_level != IPPROTO_IP)
		continue;

	  if (cmsg->cmsg_type == IP_TOS) {
		memcpy (&recv_qos, CMSG_DATA (cmsg),
				sizeof (recv_qos));
		dprintf ("TOSv4 = 0x%02" PRIx8 ";\n", recv_qos);
	  } else if (cmsg->cmsg_type == IP_TTL) {
		memcpy (&recv_ttl, CMSG_DATA (cmsg),
				sizeof (recv_ttl));
		dprintf ("TTLv4 = %i;\n", recv_ttl);
	  } else {
		dprintf ("Not handling option %i.\n",
				 cmsg->cmsg_type);
	  }
	}
  }

  pinghost *host =
	ping_receive_ipv4 (obj, payload_buffer, payload_buffer_len);
  if (host == nullptr)
	return -1;

  dprintf ("rcvd: %12i.%06i\n",
		   (int) pkt_now.tv_sec,
		   (int) pkt_now.tv_usec);
  dprintf ("sent: %12i.%06i\n",
		   (int) host->tv.tv_sec,
		   (int) host->tv.tv_usec);

  if (ping_timeval_sub (&pkt_now, &host->tv, &diff) < 0) {
	timerclear (&host->tv);
	return -1;
  }

  dprintf ("diff: %12i.%06i\n",
		   (int) diff.tv_sec,
		   (int) diff.tv_usec);

  if (recv_ttl >= 0)
	host->recv_ttl = recv_ttl;
  host->recv_qos = recv_qos;

  host->latency = ((double) diff.tv_usec) / 1000.0;
  host->latency += ((double) diff.tv_sec)  * 1000.0;

  timerclear (&host->tv);

  return 0;
}

static ssize_t ping_sendto (pingobj *obj, pinghost *ph,
							const void *buf, size_t buflen, int fd) {
  if (gettimeofday (&ph->tv, nullptr) == -1) {
	timerclear (&ph->tv);
	return -1;
  }

  ssize_t ret = sendto (fd, buf, buflen, 0,
						(struct sockaddr *) &ph->sockaddr,
						ph->addrlen);

  if (ret < 0) {
	if (errno == EHOSTUNREACH)
	  return 0;
	if (errno == ENETUNREACH)
	  return 0;
	/* BSDs return EHOSTDOWN on ARP/ND failure */
	if (errno == EHOSTDOWN)
	  return 0;
	ping_set_errno (obj, errno);
  }

  return ret;
}

static int ping_send_one_ipv4 (pingobj *obj, pinghost *ph, int fd) {
  dprintf ("ph->hostname = %s\n", ph->hostname);

  uint8_t   buf[4096] = {0};
  struct icmp *icmp4 = (struct icmp *) buf;
  icmp4->icmp_type = ICMP_ECHO;
  icmp4->icmp_id = htons (ph->ident);
  icmp4->icmp_seq = htons (ph->sequence);

  size_t datalen = ph->data.size();
  size_t buflen = ICMP_MINLEN + datalen;
  if (sizeof (buf) < buflen)
	return EINVAL;

  uint8_t *data = buf + ICMP_MINLEN;
  memcpy (data, ph->data.data(), datalen);

  icmp4->icmp_cksum = NetUtil::ICMPChecksum(buf, buflen);

  dprintf ("Sending ICMPv4 package with ID 0x%04x\n", ph->ident);

  int status = ping_sendto (obj, ph, buf, buflen, fd);
  if (status < 0) {
	perror ("ping_sendto");
	return -1;
  }

  dprintf ("sendto: status = %i\n", status);

  return 0;
}

static int ping_send_one (pingobj *obj, pinghost *ph, int fd) {
  if (gettimeofday (&ph->tv, nullptr) == -1) {
	/* start timer.. The GNU `ping6' starts the timer before
	 * sending the packet, so I will do that too */
#if WITH_DEBUG
	char errbuf[PING_ERRMSG_LEN];
	dprintf ("gettimeofday: %s\n",
			 sstrerror (errno, errbuf, sizeof (errbuf)));
#endif
	timerclear (&ph->tv);
	return -1;
  } else {
	dprintf ("timer set for hostname = %s\n", ph->hostname);
  }

  dprintf ("Sending ICMPv4 echo request to `%s'\n", ph->hostname);
  if (ping_send_one_ipv4 (obj, ph, fd) != 0) {
	timerclear(&ph->tv);
	return -1;
  }

  ph->sequence++;

  return 0;
}

/*
 * Set the TTL of a socket protocol independently.
 */
static int ping_set_ttl (pingobj *obj, int ttl) {
  int ret = 0;
  char errbuf[PING_ERRMSG_LEN];

  if (obj->fd4 != -1) {
	if (setsockopt (obj->fd4, IPPROTO_IP, IP_TTL,
					&ttl, sizeof (ttl))) {
	  ret = errno;
	  ping_set_error (obj, "ping_set_ttl",
					  sstrerror (ret, errbuf, sizeof (errbuf)));
	  dprintf ("Setting TTLv4 failed: %s\n", errbuf);
	}
  }

  return ret;
}

/*
 * Set the TOS of a socket protocol independently.
 *
 * Using SOL_SOCKET / SO_PRIORITY might be a protocol independent way to
 * set this. See socket(7) for details.
 */
static int ping_set_qos (pingobj *obj, uint8_t qos) {
  int ret = 0;
  char errbuf[PING_ERRMSG_LEN];

  if (obj->fd4 != -1) {
	dprintf ("Setting TP_TOS to %#04" PRIx8 "\n", qos);
	if (setsockopt (obj->fd4, IPPROTO_IP, IP_TOS,
					&qos, sizeof (qos))) {
	  ret = errno;
	  ping_set_error (obj, "ping_set_qos",
					  sstrerror (ret, errbuf, sizeof (errbuf)));
	  dprintf ("Setting TP_TOS failed: %s\n", errbuf);
	}
  }

  return ret;
}

static int ping_get_ident () {
  static uint16_t next_ident = 0xBEEF;
  next_ident++;
  return next_ident;
}

static pinghost *pinghost_alloc() {
  pinghost *ph = new pinghost;
  if (ph == nullptr)
	return nullptr;

  ph->addrlen = sizeof (struct sockaddr_storage);
  ph->latency = -1.0;
  ph->dropped = 0;
  ph->ident = ping_get_ident () & 0xFFFF;

  return ph;
}

static void pinghost_free(pinghost *ph) {
  delete ph;
}

/* ping_open_socket opens, initializes and returns a new raw socket to use for
 * ICMPv4. On error, -1 is returned and obj->errmsg is set appropriately. */
static int ping_open_socket() {
  std::string error;
  std::optional<int> fdo = NetUtil::MakeICMPSocket(&error);
  if (!fdo.has_value()) {
	dprintf ("couldn't open socket: %s\n", error.c_str());
	return -1;
  }

  return fdo.value();
}

/*
 * public methods
 */
const char *ping_get_error (pingobj *obj) {
  if (obj == nullptr)
	return nullptr;
  return obj->errmsg;
}

pingobj *ping_construct (int ttl, uint8_t qos) {
  pingobj *obj = new pingobj;

  if (obj == nullptr)
	return nullptr;

  obj->timeout = PING_DEF_TIMEOUT;
  for (char c : PING_DEF_DATA)
	obj->data.push_back(c);
  obj->fd4 = ping_open_socket();

  ping_set_ttl (obj, ttl);
  ping_set_qos (obj, qos);

  return obj;
}

void ping_destroy (pingobj *obj) {
  if (obj == nullptr)
	return;

  for (pinghost *host : obj->table) {
	pinghost_free(host);
  }
  obj->table.clear();

  if (obj->fd4 != -1)
	close(obj->fd4);

  delete obj;
  return;
}

int ping_setopt (pingobj *obj, int option, const void *value) {
  if ((obj == nullptr) || (value == nullptr))
	return -1;

  switch (option) {

  case PING_OPT_TIMEOUT:
	obj->timeout = *((const double *) value);
	if (obj->timeout < 0.0) {
	  obj->timeout = PING_DEF_TIMEOUT;
	  return -1;
	}
	return 0;

  default:
	return -2;
  }
}

// pings all the hosts in sequence and reads their responses
int ping_send (pingobj *obj) {
  struct timeval endtime;
  struct timeval nowtime;
  struct timeval timeout;

  for (pinghost *host : obj->table) {
	host->latency = -1.0;
	host->recv_ttl = -1;
  }

  if (obj->table.empty()) {
	ping_set_error (obj, "ping_send", "No hosts to ping");
	return -1;
  }

  CHECK(obj->fd4 != -1);

  if (gettimeofday (&nowtime, nullptr) == -1) {
	ping_set_errno (obj, errno);
	return -1;
  }

  /* Set up timeout */
  timeout.tv_sec = (time_t) obj->timeout;
  timeout.tv_usec = (suseconds_t) (1000000 * (obj->timeout -
											  ((double) timeout.tv_sec)));

  dprintf ("Set timeout to %i.%06i seconds\n",
		   (int) timeout.tv_sec,
		   (int) timeout.tv_usec);

  ping_timeval_add (&nowtime, &timeout, &endtime);

  /* pings_in_flight is the number of hosts we sent a "ping" to but didn't
   * receive a "pong" yet. */
  int pings_in_flight = 0;

  /* pongs_received is the number of echo replies received. Unless there
   * is an error, this is used as the return value of ping_send(). */
  int pongs_received = 0;

  int error_count = 0;

  // Index (in table) of next host to ping, or .size() if we are done.
  size_t next_host_idx = 0;
  while (pings_in_flight > 0 || next_host_idx < obj->table.size()) {
	fd_set read_fds;
	fd_set write_fds;
	FD_ZERO (&read_fds);
	FD_ZERO (&write_fds);

	CHECK(obj->fd4 != -1);

	FD_SET(obj->fd4, &read_fds);

	const int write_fd =
	  next_host_idx < obj->table.size() ? obj->fd4 : -1;

	const int max_fd = obj->fd4;

	if (write_fd != -1)
	  FD_SET(write_fd, &write_fds);

	CHECK (max_fd < FD_SETSIZE);

	if (gettimeofday (&nowtime, nullptr) == -1) {
	  ping_set_errno (obj, errno);
	  return -1;
	}

	if (ping_timeval_sub (&endtime, &nowtime, &timeout) == -1)
	  break;

	dprintf ("Waiting on %i sockets for %u.%06u seconds\n",
			 ((obj->fd4 != -1) ? 1 : 0),
			 (unsigned) timeout.tv_sec,
			 (unsigned) timeout.tv_usec);

	int status = select (max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
	int select_errno = errno;

	if (gettimeofday (&nowtime, nullptr) == -1) {
	  ping_set_errno (obj, errno);
	  return -1;
	}

	if (status == -1) {
	  ping_set_errno (obj, select_errno);
	  dprintf ("select: %s\n", obj->errmsg);
	  return -1;
	} else if (status == 0) {
	  dprintf ("select timed out\n");

	  for (pinghost *ph : obj->table)
		if (ph->latency < 0.0)
		  ph->dropped++;
	  break;
	}

	// If possible to read, do so.
	if (FD_ISSET (obj->fd4, &read_fds)) {
	  if (ping_receive_one (obj, &nowtime) == 0) {
		pings_in_flight--;
		pongs_received++;
	  }
	}

	// If possible to write, do so.
	if (write_fd != -1 && FD_ISSET (write_fd, &write_fds)) {
	  CHECK(next_host_idx < obj->table.size());
	  pinghost *host = obj->table[next_host_idx];
	  if (ping_send_one (obj, host, write_fd) == 0)
		pings_in_flight++;
	  else
		error_count++;
	  next_host_idx++;
	}
  }

  if (error_count)
	return -error_count;
  return pongs_received;
}

static pinghost *ping_host_search (pingobj *obj, const char *host) {
  for (pinghost *ph : obj->table)
	if (strcasecmp (ph->username.c_str(), host) == 0)
	  return ph;

  return nullptr;
}

int ping_host_add (pingobj *obj, const char *host) {
  if ((obj == nullptr) || (host == nullptr))
	return -1;

  dprintf ("host = %s\n", host);

  if (ping_host_search (obj, host) != nullptr)
	return 0;

  std::string canon, error;
  std::optional<uint32_t> ipo =
	NetUtil::GetIPV4(host, &canon, &error);
  if (!ipo.has_value()) {
	dprintf("GetIPV4 failed for %s: %s\n", host, error.c_str());
	return -1;
  }

  const uint32_t ip = ipo.value();

  pinghost *ph = pinghost_alloc();
  CHECK(ph);

  ph->username = host;
  ph->hostname = canon;
  ph->data = obj->data;

  sockaddr_in *s = (sockaddr_in*)&ph->sockaddr;
  ph->addrlen = sizeof (sockaddr_in);
  s->sin_family = AF_INET;
  s->sin_port = 0;
  uint8_t *a = (uint8_t*)&s->sin_addr;
  a[0] = (ip >> 24) & 255;
  a[1] = (ip >> 16) & 255;
  a[2] = (ip >> 8) & 255;
  a[3] = ip & 255;

  /*
	  ai_ptr->ai_socktype = SOCK_RAW;
	  ai_ptr->ai_protocol = IPPROTO_ICMP;
  */

  obj->table.push_back(ph);

  return 0;
}

const std::vector<pinghost *> &ping_gethosts(pingobj *obj) {
  return obj->table;
}

int pinghost_get_info(pinghost *host, int info,
					  void *buffer, size_t *buffer_len) {
  int ret = EINVAL;

  size_t orig_buffer_len = *buffer_len;

  if ((host == nullptr) || (buffer_len == nullptr))
	return -1;

  if ((buffer == nullptr) && (*buffer_len != 0 ))
	return -1;

  switch (info) {
  case PING_INFO_USERNAME:
	ret = ENOMEM;
	*buffer_len = host->username.size() + 1;
	if (orig_buffer_len <= *buffer_len)
	  break;
	/* Since (orig_buffer_len > *buffer_len) `strncpy'
	 * will copy `*buffer_len' and pad the rest of
	 * `buffer' with null-bytes */
	strncpy ((char*)buffer, host->username.c_str(), orig_buffer_len);
	ret = 0;
	break;

  case PING_INFO_HOSTNAME:
	ret = ENOMEM;
	*buffer_len = host->hostname.size() + 1;
	if (orig_buffer_len < *buffer_len)
	  break;
	/* Since (orig_buffer_len > *buffer_len) `strncpy'
	 * will copy `*buffer_len' and pad the rest of
	 * `buffer' with null-bytes */
	strncpy ((char*)buffer, host->hostname.c_str(), orig_buffer_len);
	ret = 0;
	break;

  case PING_INFO_ADDRESS:
	ret = getnameinfo ((struct sockaddr *) &host->sockaddr,
					   host->addrlen,
					   (char *) buffer,
					   *buffer_len,
					   nullptr, 0,
					   NI_NUMERICHOST);
	if (ret != 0) {
	  if ((ret == EAI_MEMORY) || (ret == EAI_OVERFLOW))
		ret = ENOMEM;
	  else if (ret == EAI_SYSTEM)
		ret = errno;
	  else
		ret = EINVAL;
	}
	break;

  case PING_INFO_LATENCY:
	ret = ENOMEM;
	*buffer_len = sizeof (double);
	if (orig_buffer_len < sizeof (double))
	  break;
	*((double *) buffer) = host->latency;
	ret = 0;
	break;

  case PING_INFO_DROPPED:
	ret = ENOMEM;
	*buffer_len = sizeof (uint32_t);
	if (orig_buffer_len < sizeof (uint32_t))
	  break;
	*((uint32_t *) buffer) = host->dropped;
	ret = 0;
	break;

  case PING_INFO_SEQUENCE:
	ret = ENOMEM;
	*buffer_len = sizeof (unsigned int);
	if (orig_buffer_len < sizeof (unsigned int))
	  break;
	*((unsigned int *) buffer) = (unsigned int) host->sequence;
	ret = 0;
	break;

  case PING_INFO_IDENT:
	ret = ENOMEM;
	*buffer_len = sizeof (uint16_t);
	if (orig_buffer_len < sizeof (uint16_t))
	  break;
	*((uint16_t *) buffer) = (uint16_t) host->ident;
	ret = 0;
	break;

  case PING_INFO_RECV_TTL:
	ret = ENOMEM;
	*buffer_len = sizeof (int);
	if (orig_buffer_len < sizeof (int))
	  break;
	*((int *) buffer) = host->recv_ttl;
	ret = 0;
	break;

  case PING_INFO_RECV_QOS:
	ret = ENOMEM;
	if (*buffer_len > sizeof(unsigned)) *buffer_len=sizeof(unsigned);
	if (!*buffer_len) *buffer_len=1;
	if (orig_buffer_len < *buffer_len)
	  break;
	memcpy(buffer, &host->recv_qos, *buffer_len);
	ret = 0;
	break;
  }

  return ret;
}

void *pinghost_get_context(pinghost *host) {
  if (host == nullptr)
	return nullptr;
  return host->context;
}

void pinghost_set_context(pinghost *host, void *context) {
  if (host == nullptr)
	return;
  host->context = context;
}

size_t pinghost_data_size(pinghost *host) {
  return host->data.size();
}
