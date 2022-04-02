
// (Currently fake) cue-cartridge plugin.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <string>
#include <optional>
#include <functional>
#include <memory>
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
#include "base/stringprintf.h"

// #include "viz/cueu-viz.h"
#include "hashing.h"

#include "nbd-util.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

using namespace std;
using uint8 = uint8_t;
using uint64 = uint64_t;
using int64 = int64_t;

static constexpr int BLOCK_SIZE = 512;

// TODO: Move to nbdutil or whatever
#define CHECK(e) \
  if (e) {} else NbdFatal(__FILE__, __LINE__).Stream() \
				   << "*************** Check failed: " #e "\n"

[[maybe_unused]]
static string VecBytes(const std::vector<uint8> &v) {
  string out;
  for (uint8 b : v) {
	StringAppendF(&out, "%02x", b);
  }
  return out;
}

struct Block {
  Block(int id) : id(id) {
	for (int i = 0; i < BLOCK_SIZE; i++)
	  debug_contents.push_back(0);
  }

  // XXX
  bool is_zero = true;
  std::vector<uint8> debug_contents;

  // There may not be visualization for this one if
  // running on a pi? I guess we could stream it over
  // ssh?
  //
  // must hold lock
  void Viz() {
    nbdkit_debug("CVIZ[o %d %d %d %d ]ZIVC",
                 id,
                 num_reads, num_writes,
				 is_zero ? 1 : 0);
  }

  // Read the indicated portion of the block into the buffer.
  void Read(uint8_t *buf, int start, int count) {
    std::unique_lock<std::mutex> ul(mutex);
	if (is_zero) {
	  for (int c = 0; c < count; c++) {
		buf[c] = 0x00;
	  }
	} else {
	  CHECK(debug_contents.size() == BLOCK_SIZE);
	  for (int c = 0; c < count; c++) {
		buf[c] = debug_contents[start + c];
	  }
	}
	num_reads++;
    Viz();
  }

  // Write the buffer to the indicated portion of the block.
  void Write(const uint8_t *buf, int start, int count) {
    std::unique_lock<std::mutex> ul(mutex);

	for (int c = 0; c < count; c++) {
	  debug_contents[start + c] = buf[c];
	}

	is_zero = [this]() {
		for (uint8_t b : debug_contents)
		  if (b != 0)
			return false;
		return true;
	  }();
	
	num_writes++;
	Viz();
  }

  const int id = 0;

private:

  std::mutex mutex;

  int num_reads = 0, num_writes = 0;
};

// Test version of buffer where we actually keep the backing
// store, but only provide access to it slowly.
struct Blocks {
  explicit Blocks(int64_t num_blocks) : num_blocks(num_blocks) {
    for (int i = 0; i < num_blocks; i++)
      mem.push_back(new Block(i));
  }

  // Read, into the beginning of buf, 'count' bytes from the block,
  // starting at 'start'.
  inline void Read(int block_idx, uint8_t *buf, int start, int count) {
    // nbdkit_debug("Read %d[%d,%d] -> %p\n", block_idx, start, count, buf);
    nbdkit_debug("CVIZ[r %d]ZIVC", block_idx);
    mem[block_idx]->Read(buf, start, count);
  }

  inline void Write(int block_idx, const uint8_t *buf, int start, int count) {
    // nbdkit_debug("Write %d[%d,%d] <- %p\n", block_idx, start, count, buf);
    nbdkit_debug("CVIZ[w %d]ZIVC", block_idx);
    mem[block_idx]->Write(buf, start, count);
  }

  ~Blocks() {
    for (Block *b : mem) delete b;
    mem.clear();
  }

  int NumNonzero() {
	int nonzero = 0;
	for (const Block *b : mem) {
	  if (!b->is_zero) nonzero++;
	}
	return nonzero;
  }
  
  const int64_t num_blocks = 0;
  vector<Block *> mem;
};

static int64_t num_blocks = -1;
// Allocated buffer.
static Blocks *blocks = nullptr;

static int cueu_after_fork(void) {
  nbdkit_debug("After fork! num_blocks %lu\n", num_blocks);
  blocks = new Blocks(num_blocks);
  return 0;
}

static void cueu_unload(void) {
  delete blocks;
  blocks = nullptr;
}

static int cueu_config(const char *key, const char *value) {
  if (strcmp(key, "num_bytes") == 0) {
    int64_t num_bytes = nbdkit_parse_size(value);
    if (num_bytes == -1)
      return -1;
    if (num_bytes % BLOCK_SIZE != 0) {
      nbdkit_error("bytes must be divisible by block size, %d", BLOCK_SIZE);
      return -1;
    }

    num_blocks = num_bytes / BLOCK_SIZE;
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int cueu_config_complete(void) {
  if (num_blocks == -1) {
    nbdkit_error("you must specify num_bytes=<NUM> on the command line");
    return -1;
  }
  return 0;
}

#define cueu_config_help \
  "num_bytes=<NUM>  (required) Number of bytes in the backing buffer"

static void cueu_dump_plugin(void) {
}

[[maybe_unused]]
static int cueu_get_ready(void) {
  // ?
  return 0;
}

/* Create the per-connection handle. */
static void *cueu_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t cueu_get_size(void *handle) {
  return num_blocks * BLOCK_SIZE;
}

/* Flush is a no-op, so advertise native FUA support */
static int cueu_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int cueu_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int cueu_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
// read count bytes starting at offset into buf
static int cueu_pread(void *handle, void *buf_void,
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
static int cueu_pwrite(void *handle, const void *buf_void,
                        uint32_t count, uint64_t offset,
                        uint32_t flags) {
  const uint8_t *buf = (const uint8_t*)buf_void;
  // nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);

  // Just as in the read case.
  for (;;) {
    if (count == 0)
	  break;
	
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

  int nz = blocks->NumNonzero();
  nbdkit_debug("Now %d nonzero blocks.\n", nz);
  return 0;
}

static int cueu_flush(void *handle, uint32_t flags) {
  return 0;
}

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "cueu",
  .version           = "1.0.0",
  .load              = nullptr,
  .unload            = cueu_unload,
  .config            = cueu_config,
  .config_complete   = cueu_config_complete,
  .config_help       = cueu_config_help,

  .open              = cueu_open,
  .get_size          = cueu_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  // well, you can rotate the tetris pieces? :)
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = cueu_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = cueu_can_fua,

  .pread             = cueu_pread,
  .pwrite            = cueu_pwrite,
  .flush             = cueu_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_bytes",
  .can_multi_conn    = cueu_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = cueu_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // cueu_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = nullptr,
  .after_fork        = cueu_after_fork,

};

NBDKIT_REGISTER_PLUGIN(plugin)
