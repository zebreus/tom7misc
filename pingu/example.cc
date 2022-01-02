
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "allocator.h"

// This is from config.h but I want to avoid autoconf.
#define PACKAGE_VERSION "1.29.11"

// Size of disk.
static int64_t size = -1;

// Allocated buffer.
static uint8_t *a;

static void example_unload(void) {
  free(a);
}

static int example_config(const char *key, const char *value) {
  if (strcmp(key, "size") == 0) {
    size = nbdkit_parse_size(value);
    if (size == -1)
      return -1;
  } else {
    nbdkit_error("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

static int example_config_complete(void) {
  if (size == -1) {
    nbdkit_error("you must specify size=<SIZE> on the command line");
    return -1;
  }
  return 0;
}

#define example_config_help \
  "size=<SIZE>  (required) Size of the backing buffer"

static void example_dump_plugin(void) {
}

static int example_get_ready(void) {
  nbdkit_debug("Get Ready! Size %lu\n", size);
  a = (uint8_t*) malloc(size);
  if (a == NULL)
    return -1;
  return 0;
}

/* Create the per-connection handle. */
static void *example_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the disk size. */
static int64_t example_get_size(void *handle) {
  return size;
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

/* Fast zero. */
static int example_can_fast_zero(void *handle) {
  return 1;
}

/* Read data. */
// read count bytes starting at offset into buf
static int example_pread(void *handle, void *buf,
                         uint32_t count, uint64_t offset,
                         uint32_t flags) {
  nbdkit_debug("pread(%u, %lu)\n", count, offset);
  assert(!flags);
  memcpy(buf, a + offset, count);
  return 0;
}

/* Write data. */
static int example_pwrite(void *handle, const void *buf,
                          uint32_t count, uint64_t offset,
                          uint32_t flags) {
  nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);
  memcpy(a + offset, buf, count);
  return 0;
}

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

/* Nothing is persistent, so flush is trivially supported */
static int example_flush(void *handle, uint32_t flags) {
  return 0;
}

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
  .trim              = example_trim,
  .zero              = example_zero,

  .magic_config_key  = "size",
  .can_multi_conn    = example_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = example_extents,

  .can_cache         = example_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = example_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = example_get_ready,
  .after_fork = nullptr,

};

NBDKIT_REGISTER_PLUGIN(plugin)
