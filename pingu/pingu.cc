
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

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "allocator.h"

#include "arcfour.h"
#include "threadutil.h"
#include "randutil.h"
#include "timer.h"
#include "netutil.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

static constexpr double PING_TIMEOUT = 4.0;
static constexpr int TARGET_PINGS_PER_BLOCK = 5;

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
				   << "Check failed: " #e "\n"

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


[[maybe_unused]]
static int IdFromKey(uint32_t key) {
  static_assert(BITS_FOR_ID > 0 && BITS_FOR_ID < 32);
  static constexpr uint32_t MASK = (1 << BITS_FOR_ID) - 1;
  return (key >> (32 - BITS_FOR_ID)) & MASK;
}

[[maybe_unused]]
static int MakeKey(int id, uint32_t rest) {
  static_assert(BITS_FOR_ID > 0 && BITS_FOR_ID < 32);
  static constexpr uint32_t MASK = (1 << BITS_FOR_ID) - 1;
  static constexpr uint32_t SHIFTED_MASK = MASK << (32 - BITS_FOR_ID);
  return (rest & ~SHIFTED_MASK) | ((id & MASK) << (32 - BITS_FOR_ID));
}

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
  Block(int id) : id(id) {
    bzero(contents.data(), BLOCK_SIZE);
  }

  // Most hold mutex
  void Viz() {
    nbdkit_debug("VIZ[b %d %d %d %d]ZIV",
                 id,
                 AllZero() ? 1 : 0,
                 (int)reads.size(),
                 (int)writes.size());
  }

  // Read the indicated portion of the block into the buffer. This is
  // accomplished by registering ourselves as interested in the data
  // (when it comes back from a ping) and blocking until that happens.
  void Read(uint8_t *buf, int start, int count) {
    bool read_done = false;
    std::unique_lock<std::mutex> ul(mutex);
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
    // PERF: Queueing multiple writes is not necessary as
    // we will only write one piece of data (the last one).
    // But we should be careful about returning until we
    // have "completed" the earlier writes.
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

  // Route a ping to this block, which will juggle it back
  // into the network.
  // TODO: Call this!
  // TODO: We can remove all our outstanding pings because they
  // are invalid (or timed out, etc.). If this happens then we
  // should at least fail the reads/writes so we don't just
  // deadlock.
  void ReceivedPing(const NetUtil::Ping &ping, int write_fd) {
    bool notify = false;
    {
      MutexLock ml(&mutex);

      uint32_t key = (ping.ident << 16) | ping.seq;
      auto it = outstanding.find(key);
      if (it == outstanding.end()) {
        // Wasn't found, probably because we already timed out.
        return;
      }

      [[maybe_unused]]
      OutstandingPing oping = it->second;
      outstanding.erase(it);
      // TODO: could keep stats about this host

      // Invalid if the version is wrong.
	  if (oping.version != current_version)
		return;
	  
      // Invalid if we don't have the full block as the payload.
      if (ping.data.size() != BLOCK_SIZE)
        return;

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
        pts.id = (key >> 16) & 0xFFFF;
        pts.seq = key & 0xFFFF;
        pts.ip = GetHost();

		OutstandingPing outping;
		oping.host = pts.ip;
		oping.version = current_version;
		
        if (NetUtil::SendPing(write_fd, pts)) {
		  outstanding[key] = outping;
        } else {
          // XXX recover somehow; at least making another attempt?
          nbdkit_debug("SendPing immediately failed");
        }
      }
    }
    
    if (notify) cond.notify_all();
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
  
  // Protected by the mutex.
  std::vector<PendingRead> reads;
  std::vector<PendingWrite> writes;

  bool AllZero() const {
    for (int i = 0; i < BLOCK_SIZE; i++) {
      if (contents[i] != 0) return false;
    }
    return true;
  }

  // Private! This is just for debugging; the whole point is NOT to store this.
  std::array<uint8_t, BLOCK_SIZE> contents;
};

// Just an array of blocks.
struct Blocks {
  explicit Blocks(int64_t num_blocks) : num_blocks(num_blocks) {
    CHECK(num_blocks <= (1 << BITS_FOR_ID)) << num_blocks;
    for (int i = 0; i < num_blocks; i++)
      mem.push_back(new Block(i));
  }

  inline void Read(int block_idx, uint8_t *buf, int start, int count) {
    nbdkit_debug("Read %d[%d,%d] -> %p\n", block_idx, start, count, buf);
    mem[block_idx]->Read(buf, start, count);
  }

  inline void Write(int block_idx, const uint8_t *buf, int start, int count) {
    nbdkit_debug("Write %d[%d,%d] <- %p\n", block_idx, start, count, buf);
    mem[block_idx]->Write(buf, start, count);
  }

  ~Blocks() {
    for (Block *b : mem) delete b;
    mem.clear();
  }

  const int64_t num_blocks = 0;
  std::vector<Block *> mem;
};

// Processes pending reads/writes.
struct Processor {
  Processor(Blocks *blocks) : blocks(blocks) {
    process_thread.reset(new std::thread(&Processor::ProcessThread, this));
    nbdkit_debug("thread spawned %p\n", process_thread.get());
  }

  void ProcessThread() {
    printf("in the thread");
    nbdkit_debug("process thread started\n");
    ArcFour rc("process");
    int64_t cursor = 0;

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
      // int idx = RandTo(&rc, blocks->num_blocks);
      int idx = (cursor++) % blocks->num_blocks;
      Block *block = blocks->mem[idx];

      nbdkit_debug("VIZ[u %d]ZIV", idx);

      {
        std::unique_lock<std::mutex> ul(block->mutex);
        // nbdkit_debug("%d is unlocked", idx);

        // Process all reads
        for (const Block::PendingRead &pr : block->reads) {
          // using the private data..
          for (int i = 0; i < pr.count; i++)
            pr.buf[i] = block->contents[pr.start + i];
          *pr.done = true;
        }
        block->reads.clear();

        // And all writes
        // (PERF: We only need to write the last one but
        // need to mark all as done)
        for (const Block::PendingWrite &pw : block->writes) {
          // using the private data..
          for (int i = 0; i < pw.count; i++)
            block->contents[pw.start + i] = pw.buf[i];
          *pw.done = true;
        }
        block->writes.clear();
      }

      // Lock is released. Notify anyone waiting.
      block->cond.notify_all();

      std::this_thread::sleep_for(
          std::chrono::milliseconds(10));
      {
        MutexLock ml(&mutex);
        if (should_die)
          goto die;
      }
    }

  die:;
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

static int64_t num_blocks = -1;
// Allocated buffer.
static Blocks *blocks = nullptr;

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
