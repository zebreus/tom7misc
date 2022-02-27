
// Reference in-memory plugin.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <dlfcn.h>

#include <string>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <condition_variable>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "base/stringprintf.h"
#include "cleanup.h"
#include "allocator.h"
#include "threadutil.h"
#include "randutil.h"
#include "util.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

using namespace std;
using uint8 = uint8_t;
using uint64 = uint64_t;
using int64 = int64_t;

// Protects the dynamic library handle.
static std::mutex storage_mutex;
// of num_bytes, allocated with malloc
static uint8_t *storage = nullptr;
static int64_t num_bytes = -1;

static int memu_after_fork(void) {
  nbdkit_debug("After fork! num_bytes %lu\n", num_bytes);
  std::unique_lock<std::mutex> ul(storage_mutex);
  storage = (uint8_t*)malloc(num_bytes);
  bzero(storage, num_bytes);
  return 0;
}

static void memu_unload(void) {
  std::unique_lock<std::mutex> ul(storage_mutex);
  free(storage);
  storage = nullptr;
}

static int memu_config(const char *key, const char *value) {
  if (strcmp(key, "num_bytes") == 0) {
    num_bytes = nbdkit_parse_size(value);
    if (num_bytes == -1) {
      return -1;
    }
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int memu_config_complete(void) {
  if (num_bytes == -1) {
    nbdkit_error("you must specify num_bytes=<NUM> "
                 "on the command line");
    return -1;
  }
  return 0;
}

#define memu_config_help \
  "num_bytes=<NUM>  (required) Number of bytes in the backing buffer"

static void memu_dump_plugin(void) {
}

[[maybe_unused]]
static int memu_get_ready(void) {
  // ?
  return 0;
}

/* Create the per-connection handle. */
static void *memu_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t memu_get_size(void *handle) {
  return num_bytes;
}

/* Flush is a no-op, so advertise native FUA support */
static int memu_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int memu_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int memu_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
// read count bytes starting at offset into buf
static int memu_pread(void *handle, void *buf_void,
                       uint32_t count, uint64_t offset,
                       uint32_t flags) {
  uint8_t *buf = (uint8_t*)buf_void;
  // nbdkit_debug("pread(%u, %lu)\n", count, offset);
  assert(!flags);

  std::unique_lock<std::mutex> ul(storage_mutex);
  for (int i = 0; i < (int)count; i++) {
    buf[i] = storage[offset + i];
  }
  return 0;
}

/* Write data. */
static int memu_pwrite(void *handle, const void *buf_void,
                        uint32_t count, uint64_t offset,
                        uint32_t flags) {
  [[maybe_unused]]
  const uint8_t *buf = (const uint8_t*)buf_void;
  // nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);

  std::unique_lock<std::mutex> ul(storage_mutex);
  for (int i = 0; i < (int)count; i++) {
    storage[offset + i] = buf[i];
  }
  return 0;
}

static int memu_flush(void *handle, uint32_t flags) {
  // Everything is synchronous so a flush is free.
  return 0;
}

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "memu",
  .version           = "1.0.0",
  .load              = nullptr,
  .unload            = memu_unload,
  .config            = memu_config,
  .config_complete   = memu_config_complete,
  .config_help       = memu_config_help,

  .open              = memu_open,
  .get_size          = memu_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = memu_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = memu_can_fua,

  .pread             = memu_pread,
  .pwrite            = memu_pwrite,
  .flush             = memu_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_bytes",
  .can_multi_conn    = memu_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = memu_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // memu_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = nullptr,
  .after_fork        = memu_after_fork,

};

NBDKIT_REGISTER_PLUGIN(plugin)
