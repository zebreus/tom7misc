
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <mutex>
#include <vector>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "allocator.h"

#include "threadutil.h"

// This is from config.h but I want to avoid autoconf.
#define PACKAGE_VERSION "1.29.11"

static constexpr int BLOCK_SIZE = 512;
struct Block {
  Block() {
	bzero(contents.data(), BLOCK_SIZE);
  }

  // Read the indicated portion of the block into the buffer.
  void Read(uint8_t *buf, int start, int count) {
	MutexLock ml(&mutex);
	for (int i = 0; i < count; i++) {
	  *buf = contents[start + i];
	  buf++;
	}
  }

  // Write the buffer to the indicated portion of the block.
  void Write(const uint8_t *buf, int start, int count) {
	MutexLock ml(&mutex);
	for (int i = 0; i < count; i++) {
	  contents[start + i] = *buf;
	  buf++;
	}
  }

  std::mutex mutex;
  std::array<uint8_t, BLOCK_SIZE> contents;
};

// Test version of buffer where we actually keep the backing
// store, but only provide access to it slowly.
struct Blocks {
  explicit Blocks(int64_t num_blocks) : num_blocks(num_blocks) {
	for (int i = 0; i < num_blocks; i++)
	  mem.push_back(new Block);
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

static int64_t num_blocks = -1;
// Allocated buffer.
static Blocks *blocks = nullptr;

static void example_unload(void) {
  delete blocks;
  blocks = nullptr;
}

static int example_config(const char *key, const char *value) {
  if (strcmp(key, "num_blocks") == 0) {
    num_blocks = nbdkit_parse_size(value);
    if (num_blocks == -1)
      return -1;
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int example_config_complete(void) {
  if (num_blocks == -1) {
    nbdkit_error("you must specify num_blocks=<NUM> on the command line");
    return -1;
  }
  return 0;
}

#define example_config_help \
  "num_blocks=<NUM>  (required) Number of blocks in the backing buffer"

static void example_dump_plugin(void) {
}

static int example_get_ready(void) {
  nbdkit_debug("Get Ready! Num_Blocks %lu\n", num_blocks);
  blocks = new Blocks(num_blocks);
  return 0;
}

/* Create the per-connection handle. */
static void *example_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t example_get_size(void *handle) {
  return num_blocks * BLOCK_SIZE;
}

/* Flush is a no-op, so advertise native FUA support */
static int example_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int example_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int example_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

#if 0
/* Fast zero. */
static int example_can_fast_zero(void *handle) {
  return 1;
}
#endif

/* Read data. */
// read count bytes starting at offset into buf
static int example_pread(void *handle, void *buf_void,
                         uint32_t count, uint64_t offset,
                         uint32_t flags) {
  uint8_t *buf = (uint8_t*)buf_void;
  nbdkit_debug("pread(%u, %lu)\n", count, offset);
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
static int example_pwrite(void *handle, const void *buf_void,
                          uint32_t count, uint64_t offset,
                          uint32_t flags) {
  const uint8_t *buf = (const uint8_t*)buf_void;
  nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
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

#if 0
/* Zero. */
static int example_zero(void *handle,
                        uint32_t count, uint64_t offset,
                        uint32_t flags) {
  /* Flushing, and thus FUA flag, is a no-op. Assume that
   * a->f->zero generally beats writes, so FAST_ZERO is a no-op. */
  assert((flags & ~(NBDKIT_FLAG_FUA | NBDKIT_FLAG_MAY_TRIM |
                    NBDKIT_FLAG_FAST_ZERO)) == 0);
  bzero(a + offset, count);
  return 0;
}

/* Trim (same as zero). */
static int example_trim(void *handle,
                        uint32_t count, uint64_t offset,
                        uint32_t flags) {
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);
  bzero(a + offset, count);
  return 0;
}
#endif

/* Nothing is persistent, so flush is trivially supported */
static int example_flush(void *handle, uint32_t flags) {
  return 0;
}

#if 0
/* Extents. */
static int example_extents(void *handle, uint32_t count, uint64_t offset,
                           uint32_t flags, struct nbdkit_extents *extents) {
  // Probably just for debugging? Always fails.
  return -1;

  // maybe can do it with this?
  /*
    if (nbdkit_add_extent(extents, offset, n, type) == -1)
    return -1;
  */
}
#endif

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "example",
  .version           = PACKAGE_VERSION,
  .unload            = example_unload,
  .config            = example_config,
  .config_complete   = example_config_complete,
  .config_help       = example_config_help,

  .open              = example_open,
  .get_size          = example_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = example_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = example_can_fua,

  .pread             = example_pread,
  .pwrite            = example_pwrite,
  .flush             = example_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_blocks",
  .can_multi_conn    = example_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = example_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // example_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = example_get_ready,
  .after_fork = nullptr,

};

NBDKIT_REGISTER_PLUGIN(plugin)
