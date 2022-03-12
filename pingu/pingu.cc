
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <map>
#include <cstdint>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <deque>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "allocator.h"

#include "base/stringprintf.h"
#include "arcfour.h"
#include "threadutil.h"
#include "randutil.h"
#include "timer.h"
#include "netutil.h"
#include "periodically.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

// If set, then we have a fake internal network that echos pings, for
// debugging.
#define FAKE_NET 1

using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

using Ping = NetUtil::Ping;
using PingToSend = NetUtil::PingToSend;

static constexpr double PING_TIMEOUT = 4.0;
static constexpr int TARGET_PINGS_PER_BLOCK = 10;

// Block size is same as ping payload size.
static constexpr int BLOCK_SIZE = 512;

// Number of bits to use for the block id in the ping's ident field.
// Larger values allow for larger drives, but fewer outstanding pings
// for a block and less robustness against random ping responses being
// interpreted as valid.
static constexpr int BITS_FOR_ID = 8;

// TODO: Move to nbdutil or whatever
#define CHECK(e) \
  if (e) {} else NbdFatal(__FILE__, __LINE__).Stream() \
				   << "*************** Check failed: " #e "\n"

namespace {
class NbdOstreamBuf : public std::stringbuf {
public:
  int sync() override {
	nbdkit_debug("%s", str().c_str());
	return 0;
  }
};
struct NbdFatal {
  NbdFatal(const char* file, int line) : os(&buf) {
    Stream() << file << ":" << line << ": ";
  }	
  [[noreturn]]
  ~NbdFatal() {
    Stream() << "\n" << std::flush;
    abort();
  }
  std::ostream& Stream() { return os; }
private:
  NbdOstreamBuf buf;
  std::ostream os;
};
}  // namespace

struct TokenBucket {
  TokenBucket(double max_value, double tokens_per_sec,
			  bool start_full = false) :
	max_value(max_value), tokens_per_sec(tokens_per_sec),
	last_update(std::chrono::steady_clock::now()) {
	if (start_full) tokens = max_value;
  }

  // See if we can spend 'cost' tokens, and returns true if so
  // (subtracting the tokens we spent). Refills the bucket first,
  // based on the current time.
  bool CanSpend(double cost) {
	// Refill bucket based on elapsed time
	const auto now = std::chrono::steady_clock::now();
	const std::chrono::duration<double> elapsed = now - last_update;
	last_update = now;
	const double secs = elapsed.count();
	tokens = std::min(max_value, tokens + tokens_per_sec * secs);
	if (cost <= tokens) {
	  // printf("ok with %.2fs elapsed, %.2f tokens\n", secs, tokens);
	  tokens -= cost;
	  return true;
	}
	return false;
  }
  
private:
  double tokens = 0.0;
  const double max_value = 0.0;
  const double tokens_per_sec = 0.0;
  std::chrono::time_point<std::chrono::steady_clock> last_update;
};

struct FakeNet {
  ArcFour rc;
  std::deque<Ping> fake_pings;
  TokenBucket tokens;
  
  // Lose this many out of 255 pings.
  static constexpr int LOSS = 3;
  static constexpr double PINGS_PER_SEC = 500.0;
  
  FakeNet() : rc(StringPrintf("fakenet.%lld", time(nullptr))), tokens(PINGS_PER_SEC,
																	  PINGS_PER_SEC,
																	  true) {}
  
  bool SendPing([[maybe_unused]] int fd,
				const PingToSend &sping) {
	Ping rping;
	rping.ident = sping.ident;
	rping.seq = sping.seq;
	rping.data = sping.data;
	if (rc.Byte() >= LOSS) {
	  fake_pings.push_back(rping);
	}
	return true;
  }

  std::optional<Ping>
  ReceivePing([[maybe_unused]] int fd,
			  int payload_size,
			  std::string *error = nullptr) {
	CHECK(payload_size == BLOCK_SIZE);
	if (!fake_pings.empty() && tokens.CanSpend(1.0)) {
	  Ping rping = fake_pings.front();
	  fake_pings.pop_front();
	  
	  gettimeofday(&rping.recvtime, nullptr);
	  return {rping};
	} else {
	  return std::nullopt;
	}
  }

  int Select(int maxfd, fd_set *read_fds, fd_set *write_fds,
			 void *ignored, timeval *timeout_ignored) {
	FD_ZERO(read_fds);
	FD_ZERO(write_fds);

	// we don't have the actual fd, so just set all of them in range
	for (int i = 0; i < maxfd; i++) {
	  // always ready to write
	  FD_SET(i, write_fds);
	  if (!fake_pings.empty()) {
		FD_SET(i, read_fds);
	  }
	}
	return 1;
  }
};

#if FAKE_NET
[[maybe_unused]]
static FakeNet fake_net;
#endif

static inline bool SendPing(int fd, const PingToSend &ping) {
  #if FAKE_NET
  return fake_net.SendPing(fd, ping);
  #else
  return NetUtil::SendPing(fd, ping);
  #endif
}

static inline std::optional<Ping>
ReceivePing(int fd, int payload_size,
			std::string *error = nullptr) {
  #if FAKE_NET
  return fake_net.ReceivePing(fd, payload_size, error);
  #else
  return NetUtil::ReceivePing(fd, payload_size, error);
  #endif
}

[[maybe_unused]]
static constexpr int IdFromKey(uint32_t key) {
  static_assert(BITS_FOR_ID > 0 && BITS_FOR_ID < 32);
  constexpr uint32_t MASK = (1 << BITS_FOR_ID) - 1;
  return (key >> (32 - BITS_FOR_ID)) & MASK;
}

[[maybe_unused]]
static constexpr int MakeKey(int id, uint32_t rest) {
  static_assert(BITS_FOR_ID > 0 && BITS_FOR_ID < 32);
  constexpr uint32_t MASK = (1 << BITS_FOR_ID) - 1;
  constexpr uint32_t SHIFTED_MASK = MASK << (32 - BITS_FOR_ID);
  return (rest & ~SHIFTED_MASK) | ((id & MASK) << (32 - BITS_FOR_ID));
}

static_assert(MakeKey(0x2A, 0xFFFFFFFF) == 0x2AFFFFFF);
static_assert(IdFromKey(0x2AFFFFFF) == 0x2A);

static constexpr uint32 MakeHost(int a, int b, int c, int d) {
  return ((uint32)a << 24) |
    ((uint32)b << 16) |
    ((uint32)c << 8) |
    (uint32)d;
}
static constexpr std::array HOSTS = {
  MakeHost(162, 125, 248, 18),
  MakeHost(157, 240, 2, 35),
  MakeHost(13, 32, 181, 10),
  MakeHost(205, 251, 242, 103),
  MakeHost(17, 253, 144, 10),
  MakeHost(23, 56, 211, 213),
  MakeHost(77, 88, 55, 77),  // yandex.ru
  MakeHost(182, 22, 16, 251),  // yahoo.co.jp
  MakeHost(220, 181, 38, 251),  // baidu.com
  MakeHost(140, 205, 220, 96),  // taobao.com
  MakeHost(163, 53, 76, 86),  // flipkart.com
  MakeHost(185, 60, 216, 53),  // whatsapp.com
  MakeHost(54, 239, 33, 92),  // amazon.in
  MakeHost(104, 18, 7, 4),  // youm7.com
  MakeHost(194, 232, 104, 149),  // orf.at
  MakeHost(151, 101, 2, 167),  // twitch.tv
  MakeHost(13, 107, 6, 156),  // office.com
  MakeHost(151, 101, 66, 87),  // ticketmaster.com
  MakeHost(151, 101, 65, 140),  // reddit.com
  MakeHost(64, 4, 250, 37),  // paypal.com
  MakeHost(136, 143, 190, 155),  // zoho.com
  MakeHost(13, 107, 21, 200),  // bing.com
};

struct Block {
  Block(int id) : id(id) {}

  // Most hold mutex
  void Viz() {
    nbdkit_debug("VIZ[b %d %d %d %d %d]ZIV",
                 id,
				 is_initialized ? 1 : 0,
                 (int)reads.size(),
                 (int)writes.size(),
				 (int)outstanding.size());
  }

  // Read the indicated portion of the block into the buffer. This is
  // accomplished by registering ourselves as interested in the data
  // (when it comes back from a ping) and blocking until that happens.
  void Read(uint8_t *buf, int start, int count) {
    std::unique_lock<std::mutex> ul(mutex);
	// Certainly don't want to wait for pings if we read an uninitialized
	// block, since we never sent any. Alternatively, we could kick off
	// a write of random data and then enqueue this thread?
	if (!is_initialized && writes.empty()) {
	  nbdkit_debug("uninitialized read %d", id);
	  for (int i = 0; i < count; i++)
		buf[i] = 0x2A;
	  return;
	}

    bool read_done = false;	
    reads.emplace_back(PendingRead{
          .buf = buf,
          .start = start,
          .count = count,
          .done = &read_done,
      });
    Viz();
    cond.wait(ul, [&read_done]{ return read_done; });
    Viz();
  }

  // Write the buffer to the indicated portion of the block. This is
  // similarly accomplished by registering the write and using it as
  // the transmit data when we get around to sending the ping.
  void Write(const uint8_t *buf, int start, int count) {
    bool write_done = false;
    std::unique_lock<std::mutex> ul(mutex);

    // PERF: Queueing multiple writes is often not necessary as we
    // will only write one piece of data (the last one). But we should
    // be careful about returning until we have "completed" the
    // earlier writes.
    writes.emplace_back(PendingWrite{
          .buf = buf,
          .start = start,
          .count = count,
          .done = &write_done,
      });
    Viz();
    cond.wait(ul, [&write_done]{ return write_done; });
    Viz();
  }

  // In the steady state, we write in response to receiving a ping.
  // But we also need a way to send the initial pings to the network.
  // This kicks off that process once we have a pending write.
  //
  // Returns the number of pings sent.
  int Initialize(int write_fd) {
	bool notify = false;
	int sent = 0;
	{
	  MutexLock ml(&mutex);

	  std::vector<uint32_t> keys_to_delete;
	  for (const auto &[key, op] : outstanding) {
		if (op.version != current_version) {
		  keys_to_delete.push_back(key);
		  nbdkit_debug("delete %08x because version %ld != current %ld",
					   key, op.version, current_version);
		} else if (double s = op.sent_time.Seconds(); s >= PING_TIMEOUT) {
		  nbdkit_debug("delete %08x because %.3fs elapsed", key, s);
		  keys_to_delete.push_back(key);
		}
	  }
	  for (auto key : keys_to_delete) outstanding.erase(key);

	  Viz();
	  
	  if (is_initialized) return 0;
	  if (writes.empty()) return 0;

	  // Otherwise, prep our initial data to send.
      std::vector<uint8> data(BLOCK_SIZE, 0x5D);
	  // Apply all pending writes in order.
      for (PendingWrite &pw : writes) {
        for (int i = 0; i < pw.count; i++)
          data[pw.start + i] = pw.buf[i];
        *pw.done = true;
        notify = true;
      }
	  writes.clear();
	  current_version++;

	  sent = ReplenishWithData(data, write_fd);
	  is_initialized = true;
	}
    if (notify) cond.notify_all();
	return sent;	
  }

  // While we (temporarily) know what data the full block holds, send
  // pings until we have the target number outstanding. Used both in
  // the juggling and in initialization.
  //
  // Must hold the lock.
  int ReplenishWithData(const std::vector<uint8> &data, int write_fd) {
	CHECK(data.size() == BLOCK_SIZE);
	int sent = 0;
	
	// Now, send pings to replenish up to the target. We'll
	// do at least one here.
	int outstanding_ok = 0;
	for (const auto &[key_, op] : outstanding) {
	  if (op.version == current_version &&
		  op.sent_time.Seconds() < PING_TIMEOUT) {
		outstanding_ok++;
	  }
	  // XXX otherwise just drop the oping?
	}

	const int to_send = TARGET_PINGS_PER_BLOCK - outstanding_ok;
	CHECK(to_send >= 1) << "ok " << outstanding_ok << " to_send " << to_send;
	for (int i = 0; i < to_send; i++) {
	  uint32 key = MakeKey(id, seq_counter++);

	  NetUtil::PingToSend pts;
	  pts.data = data;
	  pts.ident = (key >> 16) & 0xFFFF;
	  pts.seq = key & 0xFFFF;
	  pts.ip = GetHost();

	  OutstandingPing outping;
	  outping.host = pts.ip;
	  outping.version = current_version;
		
	  if (SendPing(write_fd, pts)) {
		CHECK(outstanding.find(key) == outstanding.end()) << key;
		outstanding[key] = outping;
		sent++;
	  } else {
		// XXX recover somehow; at least making another attempt?
		nbdkit_debug("SendPing immediately failed");
	  }
	}

	return sent;
  }
	
  
  // Route a ping to this block, which will juggle it back
  // into the network using write_fd. Returns the number of
  // pings sent.
  //
  // TODO: We can remove all our outstanding pings because they
  // are invalid (or timed out, etc.). If this happens then we
  // should at least fail the reads/writes so we don't just
  // deadlock (or it should go back to the start state
  // where it has undefined data?)
  int ReceivedPing(const NetUtil::Ping &ping, int write_fd) {
	int sent = 0;
    bool notify = false;
    {
      MutexLock ml(&mutex);

      uint32_t key = (ping.ident << 16) | ping.seq;
      auto it = outstanding.find(key);
      if (it == outstanding.end()) {
        // Wasn't found, probably because we already timed out.
        return 0;
      }

      OutstandingPing oping = it->second;
      outstanding.erase(it);
      // TODO: could keep stats about this host

      // Invalid if the version is wrong.
	  if (oping.version != current_version)
		return 0;
	  
      // Invalid if we don't have the full block as the payload.
      if (ping.data.size() != BLOCK_SIZE)
        return 0;

      // Apply pending writes to the data.
      std::vector<uint8> data = ping.data;
      for (PendingWrite &pw : writes) {
        for (int i = 0; i < pw.count; i++)
          data[pw.start + i] = pw.buf[i];
        *pw.done = true;
        notify = true;
      }

      // If we made modifications, other outstanding pings now
      // have the wrong data, so increment the block's version.
      if (!writes.empty()) {
        current_version++;
      }
      writes.clear();

      // If there are any reads, return the data to them.
      for (PendingRead &pr : reads) {
        for (int i = 0; i < pr.count; i++) {
          pr.buf[i] = data[pr.start + i];
        }
        *pr.done = true;
        notify = true;
      }
      reads.clear();

	  sent = ReplenishWithData(data, write_fd);
    }
    
    if (notify) cond.notify_all();
	return sent;
  }

  // holding lock
  uint32 GetHost() {
    const uint32 h = HOSTS[host_idx];
    host_idx++;
    if (host_idx >= (int)HOSTS.size()) host_idx = 0;
    return h;
  }
  
  // Represents a ping that we think is still waiting to come back.
  struct OutstandingPing {
    Timer sent_time;
    uint32_t host = 0;
    // TODO figure this out
    int64_t version = 0;
  };
  
  // Pending request to read this block into the buffer.
  struct PendingRead {
    uint8_t *buf;
    int start;
    int count;
    bool *done;
  };

  struct PendingWrite {
    const uint8_t *buf;
    int start;
    int count;
    bool *done;
  };
  
  const int id = 0;
  std::mutex mutex;
  std::condition_variable cond;

  // key: ident, seq
  std::map<uint32_t, OutstandingPing> outstanding;
  int64 current_version = 0;
  uint32_t seq_counter = 0;
  int host_idx = 0;
  bool is_initialized = false;
  
  // Protected by the mutex.
  std::vector<PendingRead> reads;
  std::vector<PendingWrite> writes;

};

// Just an array of blocks.
struct Blocks {
  explicit Blocks(int64_t num_blocks) : num_blocks(num_blocks) {
    CHECK(num_blocks <= (1 << BITS_FOR_ID)) << num_blocks;
    for (int i = 0; i < num_blocks; i++)
      mem.push_back(new Block(i));
  }

  void Read(int block_idx, uint8_t *buf, int start, int count) {
    nbdkit_debug("Read %d[%d,%d] -> %p\n", block_idx, start, count, buf);
    mem[block_idx]->Read(buf, start, count);
  }

  void Write(int block_idx, const uint8_t *buf, int start, int count) {
    nbdkit_debug("Write %d[%d,%d] <- %p\n", block_idx, start, count, buf);
    mem[block_idx]->Write(buf, start, count);
  }

  int Initialize(int block_idx, int write_fd) {
	return mem[block_idx]->Initialize(write_fd);
  }
  
  // Route the ping to the correct block. Return the number of pings sent.
  int ReceivedPing(const NetUtil::Ping &ping, int write_fd) {
	const uint32_t key = (ping.ident << 16) | ping.seq;
	const int block_idx = IdFromKey(key);
	// Not one of our pings! Don't crash, at least.
	if (block_idx < 0 || block_idx >= (int)mem.size())
	  return 0;
	return mem[block_idx]->ReceivedPing(ping, write_fd);
  }
  
  ~Blocks() {
    for (Block *b : mem) delete b;
    mem.clear();
  }

  const int64_t num_blocks = 0;
  std::vector<Block *> mem;
};

static int64_t num_blocks = -1;
// Allocated buffer.
static Blocks *blocks = nullptr;

// Central call to select() and routing of received pings to the appropriate
// block.
struct Processor {
  Processor(Blocks *blocks) : blocks(blocks) {
    process_thread.reset(new std::thread(&Processor::ProcessThread, this));
    nbdkit_debug("thread spawned %p\n", process_thread.get());
  }

  void ProcessThread() {
    nbdkit_debug("process thread started\n");
    // ArcFour rc("process");

	int counter = 0;
	int64_t pings_sent = 0, pings_received = 0;

	Periodically per_status(0.5);
	
	// Create IPV4 ICMP socket which we use for reading and writing.
	#if FAKE_NET
	const int fd4 = 5;
	#else
	std::string error;
	std::optional<int> fd4o = NetUtil::MakeICMPSocket(&error);
	CHECK(fd4o.has_value()) << "Must run as root! " << error;
	const int fd4 = fd4o.value();
	#endif
	
    // The structure of the loop is roughly this:
    //  - each Block has a set of outstanding pings that we think are
    //    still valid (have not timed out).
    //  - select() to see if we can read or write
    //  - for a ping we receive,
    //      - deliver its payload into any waiting thread
    //      - this decreases the outstanding count by one
    //      - so we are at least 1 below our target. send
    //        pings to get back up to the target, which
    //        should be at least one
    //  - 

    for (;;) {

	  if (per_status.ShouldRun()) {
		nbdkit_debug("VIZ[s %ld %ld %d %d]ZIV",
					 pings_sent,
					 pings_received,
					 counter,
					 TARGET_PINGS_PER_BLOCK);
	  }
	  
	  // reset these each time.
	  fd_set read_fds;
	  fd_set write_fds;
	  FD_ZERO(&read_fds);
	  FD_ZERO(&write_fds);

	  // We always want to both read and write, to juggle.
	  FD_SET(fd4, &read_fds);
	  FD_SET(fd4, &write_fds);
	  
	  const int max_fd = fd4;
	  CHECK(max_fd < FD_SETSIZE);

	  // This doesn't matter that much since we expect to be almost
	  // constantly reading and writing.
	  timeval timeout;
	  timeout.tv_sec = 1;
	  timeout.tv_usec = 0;

	  #if FAKE_NET
	  int status = fake_net.Select(max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
	  #else
	  #error unexpected XXX
	  int status = select(max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
	  #endif
	  CHECK(status != -1);

	  // If we timed out, we're probably toast, since probably all our pings
	  // failed (for every block!) but not much we can do, so just keep
	  // waiting.
	  if (status == 0) {
		nbdkit_debug("Select timeout\n");
		continue;
	  }

	  // Initialize every block, round-robin, if it needs it.
	  // PERF: Avoid repeatedly doing this, which is not harmful, but is wasteful.
	  if (FD_ISSET(fd4, &write_fds)) {
		// nbdkit_debug("init %d", counter);
		pings_sent += blocks->Initialize(counter, fd4);
		counter++;
		counter %= num_blocks;
	  }
	  
	  
	  // We'll only do something if we receive a ping. But we also need to
	  // be ready to write, so check both.
	  if (FD_ISSET(fd4, &read_fds) && FD_ISSET(fd4, &write_fds)) {
		std::optional<NetUtil::Ping> pingo = ReceivePing(fd4, BLOCK_SIZE);

		if (pingo.has_value()) {
		  const NetUtil::Ping &ping = pingo.value();
		  // nbdkit_debug("got ping %04x.%04x", ping.ident, ping.seq);		  
		  pings_received++;
		  pings_sent += blocks->ReceivedPing(ping, fd4);
		}
	  }

      {
        MutexLock ml(&mutex);
        if (should_die)
		  break;
      }
    }

	nbdkit_debug("process thread shutdown\n");
  }

  ~Processor() {
    {
      MutexLock ml(&mutex);
      should_die = true;
    }
    process_thread->join();
    process_thread.reset(nullptr);
  }

  std::mutex mutex;
  bool should_die = false;
  std::unique_ptr<std::thread> process_thread;
  Blocks *blocks = nullptr;
};

static Processor *processor = nullptr;

static int pingu_after_fork(void) {
  nbdkit_debug("After fork! num_blocks %lu\n", num_blocks);
  blocks = new Blocks(num_blocks);
  processor = new Processor(blocks);
  return 0;
}

static void pingu_unload(void) {
  delete processor;
  processor = nullptr;

  delete blocks;
  blocks = nullptr;
}

static int pingu_config(const char *key, const char *value) {
  if (strcmp(key, "num_bytes") == 0) {
    int64_t num_bytes = nbdkit_parse_size(value);
    if (num_bytes == -1)
      return -1;
    if (num_bytes % BLOCK_SIZE != 0) {
      nbdkit_error("bytes must be divisible by block size, %d", BLOCK_SIZE);
      return -1;
    }

    num_blocks = num_bytes / BLOCK_SIZE;

    if (num_blocks > (1 << BITS_FOR_ID)) {
      nbdkit_error("Too many blocks to fit in BITS_FOR_ID (%d) bits: %ld",
                   BITS_FOR_ID, num_blocks);
      return -1;
    }
    
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int pingu_config_complete(void) {
  if (num_blocks == -1) {
    nbdkit_error("you must specify num_bytes=<NUM> on the command line");
    return -1;
  }
  return 0;
}

#define pingu_config_help \
  "num_bytes=<NUM>  (required) Number of bytes in the backing buffer"

static void pingu_dump_plugin(void) {
}

[[maybe_unused]]
static int pingu_get_ready(void) {
  // ?
  return 0;
}

/* Create the per-connection handle. */
static void *pingu_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t pingu_get_size(void *handle) {
  return num_blocks * BLOCK_SIZE;
}

/* Flush is a no-op, so advertise native FUA support */
static int pingu_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int pingu_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int pingu_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
// read count bytes starting at offset into buf
static int pingu_pread(void *handle, void *buf_void,
                         uint32_t count, uint64_t offset,
                         uint32_t flags) {
  uint8_t *buf = (uint8_t*)buf_void;
  // nbdkit_debug("pread(%u, %lu)\n", count, offset);
  assert(!flags);

  // one block at a time
  for (;;) {
    if (count == 0)
      return 0;

    // Read from the first block in the range
    //
    // [---------------][--------------...
    //         ^
    // [-skip--|-rest--]
    int block_idx = offset / BLOCK_SIZE;
    int skip = offset % BLOCK_SIZE;
    int rest = BLOCK_SIZE - skip;
    // But don't read beyond requested count
    int bytes_to_read = std::min(rest, (int)count);

    blocks->Read(block_idx, buf, skip, bytes_to_read);
    // Prepare for next iteration
    count -= bytes_to_read;
    buf += bytes_to_read;
    offset += bytes_to_read;
  }

}

/* Write data. */
static int pingu_pwrite(void *handle, const void *buf_void,
                          uint32_t count, uint64_t offset,
                          uint32_t flags) {
  const uint8_t *buf = (const uint8_t*)buf_void;
  // nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);

  // Just as in the read case.
  for (;;) {
    if (count == 0)
      return 0;

    int block_idx = offset / BLOCK_SIZE;
    int skip = offset % BLOCK_SIZE;
    int rest = BLOCK_SIZE - skip;
    int bytes_to_read = std::min(rest, (int)count);

    blocks->Write(block_idx, buf, skip, bytes_to_read);
    // Prepare for next iteration
    count -= bytes_to_read;
    buf += bytes_to_read;
    offset += bytes_to_read;
  }
}

/* Nothing is persistent, so flush is trivially supported */
static int pingu_flush(void *handle, uint32_t flags) {
  return 0;
}

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "pingu",
  .version           = "1.0.0",
  .load              = nullptr,
  .unload            = pingu_unload,
  .config            = pingu_config,
  .config_complete   = pingu_config_complete,
  .config_help       = pingu_config_help,

  .open              = pingu_open,
  .get_size          = pingu_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = pingu_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = pingu_can_fua,

  .pread             = pingu_pread,
  .pwrite            = pingu_pwrite,
  .flush             = pingu_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_bytes",
  .can_multi_conn    = pingu_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = pingu_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // pingu_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = nullptr,
  .after_fork        = pingu_after_fork,

};

NBDKIT_REGISTER_PLUGIN(plugin)
