
#include <string>
#include <vector>
#include <array>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/time.h>
#include <inttypes.h>
#include <unistd.h>

#include "../netutil.h"
#include "base/stringprintf.h"
#include "base/logging.h"

#include "arcfour.h"
#include "randutil.h"

using namespace std;

static constexpr int PAYLOAD_SIZE = 512;
static constexpr time_t TIMEOUT_SEC = 4;

struct Host {
  uint8_t b = 0, c = 0, d = 0;
  // 0 = no response. saturates at 255.
  uint8_t msec = 0;
};

struct OutstandingPing {
  Host host;
  int host_idx = 0;
  timeval sent;
  // Keep this, or just a hash?
  std::array<uint8, PAYLOAD_SIZE> data;
};

[[maybe_unused]]
static double TimevalDiff(const struct timeval &a,
						  const struct timeval &b) {
  return (double)(b.tv_sec - a.tv_sec) +
	((double)(b.tv_usec - a.tv_usec) / 1e6);
}

struct Ping {
  uint16_t ident = 0;
  uint16_t seq = 0;
  struct timeval recvtime;
  std::array<uint8, PAYLOAD_SIZE> data;
};

static std::optional<Ping> Receive(int fd) {
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
	printf("recvfrom failed\n");
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
	  if (cmsg->cmsg_type == SO_TIMESTAMP)
		memcpy(&ping.recvtime, CMSG_DATA (cmsg), sizeof (timeval));
	  got_timestamp = true;
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
	CHECK(gettimeofday (&ping.recvtime, nullptr) != -1);
  }

  {
	size_t buffer_len = payload_buffer_len;
	uint8_t *buffer = payload_buffer;

	if (buffer_len < sizeof (struct ip))
	  return {};

	struct ip *ip_hdr = (struct ip *) buffer;
	size_t ip_hdr_len = ip_hdr->ip_hl << 2;

	if (buffer_len < ip_hdr_len)
	  return {};

	buffer += ip_hdr_len;
	buffer_len -= ip_hdr_len;

	if (buffer_len < ICMP_MINLEN)
	  return {};

	struct icmp *icmp_hdr = (struct icmp *) buffer;
	if (icmp_hdr->icmp_type != ICMP_ECHOREPLY) {
	  printf("ICMP not ECHOREPLY: %d\n", (int)icmp_hdr->icmp_type);
	  return {};
	}

	uint16_t recv_checksum = icmp_hdr->icmp_cksum;
	/* This writes to buffer. */
	icmp_hdr->icmp_cksum = 0;
	uint16_t calc_checksum = NetUtil::ICMPChecksum(buffer, buffer_len);

	if (recv_checksum != calc_checksum) {
	  printf("Wrong checksum: Got 0x%04" PRIx16 ", "
			   "calculated 0x%04" PRIx16 "\n",
			   recv_checksum, calc_checksum);
	  return {};
	}

	ping.ident = ntohs(icmp_hdr->icmp_id);
	ping.seq = ntohs(icmp_hdr->icmp_seq);

	// Skip to data.
	buffer_len -= ICMP_MINLEN;
	buffer += ICMP_MINLEN;
	if (buffer_len != PAYLOAD_SIZE) {
	  printf("Wrong data size: Got %d want %d\n",
			 (int)buffer_len, (int)PAYLOAD_SIZE);
	  return {};
	}

	memcpy(ping.data.data(), buffer, PAYLOAD_SIZE);
  }

  return ping;
}


static void Pingy(uint8_t a) {
  ArcFour rc(StringPrintf("pingy.%d", (int)a));
  string error;
  std::optional<int> fd4o = NetUtil::MakeICMPSocket(&error);
  CHECK(fd4o.has_value()) << "Must run as root! " << error;
  const int fd4 = fd4o.value();

  // Ping all hosts in a random order.
  vector<Host> hosts;
  hosts.reserve(256 * 256 * 256);
  for (int b = 0; b < 256; b++) {
	for (int c = 0; c < 256; c++) {
	  for (int d = 0; d < 256; d++) {
		Host h;
		h.b = b;
		h.c = c;
		h.d = d;
		h.msec = 0;
		hosts.push_back(h);
	  }
	}
  }

  Shuffle(&rc, &hosts);

  printf("Start pinging...\n");

  size_t next_idx = 0;
  // key is ident << 16 | seq.
  std::unordered_map<uint32_t, OutstandingPing> outstanding;
  while (!outstanding.empty() || next_idx < hosts.size()) {
	fd_set read_fds;
	fd_set write_fds;
	FD_ZERO (&read_fds);
	FD_ZERO (&write_fds);

	CHECK(fd4 != -1);

	FD_SET(fd4, &read_fds);

	// We'll try to write unless we already finished the table.
	// (Or cap to some max outstanding?)
	const int write_fd = next_idx < outstanding.size() ? fd4 : -1;
	if (write_fd != -1)
	  FD_SET(write_fd, &write_fds);

	const int max_fd = fd4;
	CHECK (max_fd < FD_SETSIZE);

	/* Set up timeout for select */
	timeval timeout;
	CHECK(gettimeofday (&timeout, nullptr) != -1);
	timeout.tv_sec += TIMEOUT_SEC;
	timeout.tv_usec = 0;

	int status = select (max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
	CHECK(status != -1);

	// CHECK(gettimeofday (&nowtime, nullptr) != -1);

	if (status == 0) {
	  // We read nothing within the timeout. This means all the
	  // outstanding pings have timed out.
	  for (const auto &[identseq_, op] : outstanding) {
		// (could check though...)
		hosts[op.host_idx].msec = 0;
	  }
	  outstanding.clear();
	  continue;
	}



	// If possible to read, do so.
	if (FD_ISSET (fd4, &read_fds)) {
	  std::optional<Ping> ping = Receive(fd4);
	  if (ping.has_value()) {
		// XXX Find match, etc.
	  }
	}

	#if 0
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
	#endif
  }

  close(fd4);
}

int main(int argc, char **argv) {

  Pingy(162);

  return 0;
}
