
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

static uint8_t GetByteStub(int idx) {
  // nbdkit_error("get_byte was not loaded");
  // abort();
  return 0;
}

static uint8_t GetByteDeleted(int idx) {
  nbdkit_error("get_byte was deleted");
  abort();
  return 0;
}

// Protects the dynamic library handle.
static std::mutex dl_mutex;
static void *dl_handle = nullptr;
static uint8_t (*get_byte)(int idx) = &GetByteStub;

static int64_t num_bytes = -1;

static int compu_after_fork(void) {
  nbdkit_debug("After fork! num_bytes %lu\n", num_bytes);
  return 0;
}

static void compu_unload(void) {
  std::unique_lock<std::mutex> ul(dl_mutex);
  if (dl_handle) {
    dlclose(dl_handle);
  }
  dl_handle = nullptr;
  get_byte = &GetByteDeleted;
}

static int compu_config(const char *key, const char *value) {
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

static int compu_config_complete(void) {
  if (num_bytes == -1) {
    nbdkit_error("you must specify num_bytes=<NUM> "
                 "on the command line");
    return -1;
  }
  return 0;
}

#define compu_config_help \
  "num_bytes=<NUM>  (required) Number of bytes in the backing buffer"

static void compu_dump_plugin(void) {
}

[[maybe_unused]]
static int compu_get_ready(void) {
  // ?
  return 0;
}

/* Create the per-connection handle. */
static void *compu_open(int readonly) {
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t compu_get_size(void *handle) {
  return num_bytes;
}

/* Flush is a no-op, so advertise native FUA support */
static int compu_can_fua(void *handle) {
  return NBDKIT_FUA_NATIVE;
}

/* Serves the same data over multiple connections. */
static int compu_can_multi_conn(void *handle) {
  return 1;
}

/* Cache. */
static int compu_can_cache(void *handle) {
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
// read count bytes starting at offset into buf
static int compu_pread(void *handle, void *buf_void,
                       uint32_t count, uint64_t offset,
                       uint32_t flags) {
  uint8_t *buf = (uint8_t*)buf_void;
  // nbdkit_debug("pread(%u, %lu)\n", count, offset);
  assert(!flags);

  std::unique_lock<std::mutex> ul(dl_mutex);
  for (int i = 0; i < (int)count; i++) {
    buf[i] = (*get_byte)(offset + i);
  }
  return 0;
}

// must hold lock
static std::string MakeSource(const uint8_t *buf,
                              uint32_t count, uint64_t offset) {
  std::string out =
    "#include <stdint.h>\n"
    "\n"
    "uint8_t get_byte_dl(int idx) {\n"
    "switch (idx) {\n"
    "default:\n";

  int last_byte = -1;
  for (int i = 0; i < num_bytes; i++) {

	const int this_byte =
	  // newly written byte
	  (i >= (int)offset && i < (int)(offset + count)) ?
	  buf[i - offset] :
	  // from compiled-in program
	  (*get_byte)(i);

	if (this_byte != last_byte)
	  StringAppendF(&out, " return %d;\n", last_byte);

	last_byte = this_byte;
	StringAppendF(&out, "case %d:\n", i);
  }
  StringAppendF(&out, " return %d;\n", last_byte);
  StringAppendF(&out,
                "}\n"
                "}\n");
  return out;
}

/* Write data. */
static int compu_pwrite(void *handle, const void *buf_void,
                        uint32_t count, uint64_t offset,
                        uint32_t flags) {
  [[maybe_unused]]
  const uint8_t *buf = (const uint8_t*)buf_void;
  // nbdkit_debug("pwrite(%u, %lu)\n", count, offset);
  /* Flushing, and thus FUA flag, is a no-op */
  assert((flags & ~NBDKIT_FLAG_FUA) == 0);

  std::unique_lock<std::mutex> ul(dl_mutex);
  std::string source = MakeSource(buf, count, offset);

  nbdkit_debug("%d bytes of source", (int)source.size());

  Util::WriteFile("compu-tmp.c", source);
  int status =
    system("gcc -O3 -march=native -fPIC -shared "
           "compu-tmp.c -o compu-tmp.so");
  if (status != 0) {
    nbdkit_error("compilation failed!");
    return -1;
  }

  nbdkit_debug("compiled");

  if (dl_handle != nullptr)
    dlclose(dl_handle);

  dl_handle = dlopen("./compu-tmp.so", RTLD_NOW);
  if (dl_handle == nullptr) {
    nbdkit_error("dlopen failed\n");
    return -1;
  }

  dlerror();

  get_byte = (uint8_t (*)(int))dlsym(dl_handle, "get_byte_dl");

  char *error = dlerror();
  if (error != nullptr) {
    nbdkit_error("dlsym failed: %s\n", error);
    return -1;
  }

  nbdkit_debug("symbol loaded");

  return 0;
}

static int compu_flush(void *handle, uint32_t flags) {
  // Everything is synchronous so a flush is free.
  return 0;
}

// Note: In C++, these fields have to be in the same order in which
// they are declared in nbdkit-plugin.h.
static struct nbdkit_plugin plugin = {
  .name              = "compu",
  .version           = "1.0.0",
  .load              = nullptr,
  .unload            = compu_unload,
  .config            = compu_config,
  .config_complete   = compu_config_complete,
  .config_help       = compu_config_help,

  .open              = compu_open,
  .get_size          = compu_get_size,

  .can_write = nullptr,
  .can_flush = nullptr,
  .is_rotational = nullptr,
  .can_trim = nullptr,

  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
  .dump_plugin       = compu_dump_plugin,

  .can_zero = nullptr,
  .can_fua           = compu_can_fua,

  .pread             = compu_pread,
  .pwrite            = compu_pwrite,
  .flush             = compu_flush,
  .trim              = nullptr,
  .zero              = nullptr,

  .magic_config_key  = "num_bytes",
  .can_multi_conn    = compu_can_multi_conn,

  .can_extents       = nullptr,
  .extents           = nullptr,

  .can_cache         = compu_can_cache,
  .cache             = nullptr,

  .thread_model      = nullptr,

  .can_fast_zero     = nullptr, // compu_can_fast_zero,

  .preconnect        = nullptr,

  .get_ready         = nullptr,
  .after_fork        = compu_after_fork,

};

NBDKIT_REGISTER_PLUGIN(plugin)
