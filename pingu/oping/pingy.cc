
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/time.h>
#include <inttypes.h>
#include <unistd.h>

#include <span>
#include <string>
#include <vector>
#include <array>
#include <deque>

#include "../netutil.h"
#include "base/stringprintf.h"
#include "base/logging.h"

#include "arcfour.h"
#include "randutil.h"
#include "image.h"
#include "geom/hilbert-curve.h"
#include "util.h"
#include "crypt/sha256.h"

using namespace std;

static constexpr int PAYLOAD_SIZE = 56;  // XXX
static constexpr time_t TIMEOUT_SEC = 6;
static constexpr int MAX_OUTSTANDING = 65536;
static constexpr int HASH_BYTES = 16;
static_assert(HASH_BYTES > 0 && HASH_BYTES <= SHA256::DIGEST_LENGTH);

static constexpr uint8_t TIMEOUT = 0;
static constexpr uint8_t WRONG_DATA = 255;

// We use a fixed first octet, so this struct is 32 bits.
struct Host {
  uint8_t a = 0, b = 0, d = 0;
  // 0 = no response.
  // 255 = response with wrong data
  // Otherwise, the millisecond timeout / 8, saturating at 254.
  uint8_t msec_div_8 = 0;
};

struct OutstandingPing {
  uint32_t ip = 0;
  int host_idx = 0;
  timeval sent;
  // Keep this, or just a hash?
  std::array<uint8, HASH_BYTES> data_hash;
};

static std::array<uint8, HASH_BYTES> GetHash(const std::vector<uint8_t> &data) {
  CHECK(data.size() == PAYLOAD_SIZE);
  std::vector<uint8> h = SHA256::HashPtr(data.data(), PAYLOAD_SIZE);
  std::array<uint8, HASH_BYTES> ret;
  for (int i = 0; i < HASH_BYTES; i++) ret[i] = h[i];
  return ret;
}

[[maybe_unused]]
static double TimevalDiff(const struct timeval &a,
						  const struct timeval &b) {
  return (double)(b.tv_sec - a.tv_sec) +
	((double)(b.tv_usec - a.tv_usec) / 1e6);
}

static void Pingy(uint8_t c) {
  ArcFour rc(StringPrintf("pingy.%lld.%d", (int64_t)time(nullptr), (int)c));
  string error;
  std::optional<int> fd4o = NetUtil::MakeICMPSocket(&error);
  CHECK(fd4o.has_value()) << "Must run as root! " << error;
  const int fd4 = fd4o.value();

  // Ping all hosts in a random order.
  vector<Host> hosts;
  hosts.reserve(256 * 256 * 256);
  for (int a = 0; a < 256; a++) {
	for (int b = 0; b < 256; b++) {
	  for (int d = 0; d < 256; d++) {
		Host h;
		h.a = a;
		h.b = b;
		h.d = d;
		h.msec_div_8 = TIMEOUT;
		hosts.push_back(h);
	  }
	}
  }

  Shuffle(&rc, &hosts);

  printf("Start pinging...\n");

  size_t next_idx = 0;
  // key is ident << 16 | seq.
  std::unordered_map<uint32_t, OutstandingPing> outstanding;
  // keys from outstanding; ordered by sent time. Might contain
  // keys that have been removed.
  std::deque<uint32_t> timeout_queue;

  const int64_t start_time = time(nullptr);
  
  int64 successes = 0, select_timeouts = 0, queue_timeouts = 0,
	other_errors = 0;
  auto Status = [start_time, &next_idx, &hosts, &outstanding, &timeout_queue,
				 &successes, &select_timeouts, &queue_timeouts,
				 &other_errors]() {
	  const int64_t elapsed = (int64_t)time(nullptr) - start_time;
	  const double done_per_sec = next_idx / (double)elapsed;
	  double sec_remaining = (hosts.size() - next_idx) / done_per_sec;
	  double pct = (100.0 * next_idx) / (double)hosts.size();
	  double total_done = successes + select_timeouts + queue_timeouts +
		other_errors;
	  double spct = (100.0 * successes) / total_done;
	  double stpct = (100.0 * select_timeouts) / total_done;
	  double qtpct = (100.0 * queue_timeouts) / total_done;
	  [[maybe_unused]]
	  double opct = (100.0 * other_errors) / total_done;
	  return StringPrintf(
		  "%.1fm %dk/%dk %.1f%% %d o %d q | %.1f%% ok %.1f%% st %.1f%% qt",
		  sec_remaining / 60.0,
		  (int)next_idx / 1024, (int)hosts.size() / 1024,
		  pct, outstanding.size(),
		  timeout_queue.size(),
		  spct, stpct, qtpct);
	};

  int64_t last_report = 0;
  auto Report = [&Status, &last_report](const std::function<std::string()> &f,
										bool force = false) {
	  const int64_t now = time(nullptr);
	  if (force || now > last_report) {
		printf("[%s] %s\n", Status().c_str(), f().c_str());
		last_report = now;
	  }
	};
  
  while (!outstanding.empty() || next_idx < hosts.size()) {
	fd_set read_fds;
	fd_set write_fds;
	FD_ZERO (&read_fds);
	FD_ZERO (&write_fds);

	CHECK(fd4 != -1);

	FD_SET(fd4, &read_fds);

	// We'll try to write unless we already finished the table.
	// (Or cap to some max outstanding?)

	const bool ok_to_write = (next_idx < hosts.size()) &&
	  (outstanding.size() < MAX_OUTSTANDING);

	if (ok_to_write)
	  FD_SET(fd4, &write_fds);

	const int max_fd = fd4;
	CHECK (max_fd < FD_SETSIZE);

	/* Set up timeout for select */
	timeval timeout;
	timeout.tv_sec = TIMEOUT_SEC;
	timeout.tv_usec = 0;

	int status = select(max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
	CHECK(status != -1);

	if (status == 0) {
	  Report([&outstanding]{
		  return StringPrintf("%d select timeout", outstanding.size());
		});
		
	  // We read nothing within the timeout. This means all the
	  // outstanding pings have timed out.
	  for (const auto &[identseq_, oping] : outstanding) {
		// (could check though...)
		hosts[oping.host_idx].msec_div_8 = TIMEOUT;
		select_timeouts++;
	  }
	  outstanding.clear();
	  timeout_queue.clear();
	  continue;
	}


	// If possible to read, do so.
	if (FD_ISSET(fd4, &read_fds)) {
	  // TODO: Could get error message here and filter to interesting
	  // ones.
	  std::optional<NetUtil::Ping> pingo =
		NetUtil::ReceivePing(fd4, PAYLOAD_SIZE);
	  if (pingo.has_value()) {
		const NetUtil::Ping &ping = pingo.value();
		uint32_t key = (ping.ident << 16) | ping.seq;
		auto it = outstanding.find(key);
		if (it == outstanding.end()) {
		  // Corrupt packet, duplicate, etc.
		  // These don't count towards "total".
		  Report([&ping]{
			  return StringPrintf("Dropping ping %04x|%04x with no match?\n",
								  ping.ident, ping.seq);
			}, true);
		} else {
		  const OutstandingPing &oping = it->second;
		  double sec = TimevalDiff(oping.sent, ping.recvtime);
		  if (oping.data_hash == GetHash(ping.data)) {
			successes++;
			Report([&oping, sec]{
				return StringPrintf(
					"%s took %.1f ms",
					NetUtil::IPToString(oping.ip).c_str(), sec * 1000.0);
			  });

			// 8 bits for msec/8.
			// 0 is used for no response.
			const int msec = std::clamp(1, (int)round(sec * 1000.0 / 8.0), 254);
			hosts[oping.host_idx].msec_div_8 = msec;
		  } else {
			Report([&oping, sec]{
				return StringPrintf("%s had wrong data! %.7f sec\n",
									NetUtil::IPToString(oping.ip).c_str(),
									sec);
			  }, true);
			other_errors++;
			hosts[oping.host_idx].msec_div_8 = WRONG_DATA;
		  }
		  // either way, remove it from pending pings
		  outstanding.erase(it);
		}
	  } else {
		// printf("Receive failed\n");
	  }
	}

	// Remove pings via timeout.

	{
	  timeval now;
	  CHECK(gettimeofday(&now, 0) != -1);
	  while (!timeout_queue.empty()) {
		// but leave it in queue for now...
		const uint32_t key = timeout_queue.front();

		auto it = outstanding.find(key);
		// This happens if we received a response and deleted it from
		// the outstanding table before the timeout; normal.
		if (it == outstanding.end()) {
		  timeout_queue.pop_front();
		  continue;
		}

		const OutstandingPing &oping = it->second;
		const double sec = TimevalDiff(oping.sent, now);
		if (sec > TIMEOUT_SEC) {
		  queue_timeouts++;
		  Report([&oping, sec](){
			  return StringPrintf("%s timed out (%.4f s)\n",
								  NetUtil::IPToString(oping.ip).c_str(), sec);
			});
		  hosts[oping.host_idx].msec_div_8 = TIMEOUT;
		  it = outstanding.erase(it);
		  timeout_queue.pop_front();
		  // Might be more.
		  continue;
		} else {
		  // Since they are sorted by sent time, there will
		  // be no more timeouts. Leave in queue.
		  break;
		}
	  }
	}

	// If possible to write, do so.
	if (ok_to_write && FD_ISSET(fd4, &write_fds)) {
	  CHECK(next_idx < hosts.size());
	  const int host_idx = next_idx;
	  next_idx++;

	  Host host = hosts[host_idx];
	  const uint32_t ip = NetUtil::OctetsToIP(host.a, host.b, c, host.d);
	  // want these to be globally unique; fortunately
	  // each host has a distinct IP
	  const uint16_t id = (ip >> 16) & 0xFFFF;
	  const uint16_t seq = ip & 0xFFFF;
	  const uint32_t key = (id << 16) | seq;
	  	  
	  NetUtil::PingToSend ping_to_send;
	  ping_to_send.ip = ip;
	  ping_to_send.id = id;
	  ping_to_send.seq = seq;
	  for (int i = 0; i < PAYLOAD_SIZE; i++) {
		ping_to_send.data.push_back(rc.Byte());
	  }

	  OutstandingPing outstanding_ping;
	  outstanding_ping.ip = ip;
	  outstanding_ping.host_idx = host_idx;
	  outstanding_ping.data_hash = GetHash(ping_to_send.data);
	  
	  // compute timestamp right before sending
	  CHECK(gettimeofday(&outstanding_ping.sent, nullptr) != -1);
	  (void)NetUtil::SendPing(fd4, ping_to_send);

	  CHECK(outstanding.find(key) == outstanding.end()) << key;	  
	  outstanding[key] = outstanding_ping;
	  timeout_queue.push_back(key);
	}
  }

  close(fd4);

  {
	printf("Write raw:\n");
	std::vector<uint8_t> output;
	output.resize(256 * 256 * 256);
	for (const Host &host : hosts) {
	  uint32_t pos = ((uint32)host.a << 16) | ((uint32)host.b << 8) | host.d;
	  output[pos] = host.msec_div_8;
	}

	string filename = StringPrintf("ping%d.dat", (int)c);
	Util::WriteFileBytes(filename, output);
  }

  // fairly silly with *.*.c.*
  {
	printf("Write image:\n");
	ImageRGBA img(4096, 4096);
	img.Clear32(0x7F0000FF);
	for (const Host &host : hosts) {
	  uint32_t pos = ((uint32)host.a << 16) | ((uint32)host.b << 8) | host.d;
	  const auto [x, y] = HilbertCurve::To2D(12, pos);
	  CHECK(x < 4096);
	  CHECK(y < 4096);
	  if (host.msec_div_8 == TIMEOUT) {
		img.SetPixel32(x, y, 0x000033FF);
	  } else if (host.msec_div_8 == WRONG_DATA) {
		img.SetPixel32(x, y, 0xAA0000FF);
	  } else {
		uint8_t v = host.msec_div_8;
		img.SetPixel(x, y, v, v, v, 0xFF);
	  }
	}
	string filename = StringPrintf("subnet.a.b.%d.d.png", (int)c);
	img.Save(filename);
  }
}

int main(int argc, char **argv) {
  for (int c = 2; c < 12; c++) {
	std::string filename = StringPrintf("ping%d.dat", c);
	printf(" === *.*.%d.* ===\n", c);
	if (Util::ExistsFile(filename)) {
	  printf("Already done!\n");
	} else {
	  Pingy(c);
	}
  }

  return 0;
}
