
#ifndef _CC_LIB_CONTIGUOUS_BUFFER_H
#define _CC_LIB_CONTIGUOUS_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

#include "base/logging.h"

// Byte queue (e.g. for buffered network I/O) that supports appending
// to its end and removing from its beginning. It can grow to
// arbitrary size, but avoids copying. It always represents the data
// contiguously in memory.
struct ContiguousBuffer {
  explicit ContiguousBuffer(int initial_size = 16384) {
    allocated = initial_size;
    buf = (uint8_t*)malloc(initial_size);
  }

  ~ContiguousBuffer() {
    free(buf);
  }

  bool empty() const { return end == start; }
  void clear() { start = end = 0; }
  size_t size() const { return end - start; }
  // These references are invalidated by Append and RemovePrefix, of
  // course.
  const uint8_t *data() const { return buf + start; }
  std::span<const uint8_t> Span() const {
    return std::span<const uint8_t>(data(), size());
  }

  uint8_t operator[](size_t idx) const {
    return data()[idx];
  }

  void RemovePrefix(size_t n) {
    CHECK(n <= size());
    start += n;
  }

  void Append(std::span<const uint8_t> data) {
    if (start == end && start > 0) {
      // The buffer is empty but we have slack; move the cursor
      // back to the beginning.
      start = end = 0;
    }

    // Nothing to do, and we need to avoid passing a null
    // pointer to memcpy.
    if (data.empty())
      return;

    // If we would run off the end of the buffer, we either grow
    // or compact.
    const size_t nbytes = end - start;
    if (nbytes + data.size() > allocated) {
      // Need to grow the vector to accommodate.
      // XXX PERF realloc
      const size_t new_alloc = nbytes + data.size() + (allocated >> 1);
      uint8_t *new_buf = (uint8_t*)malloc(new_alloc);

      // Always copy to the beginning of the buffer.
      memcpy(new_buf, buf + start, nbytes);
      start = 0;
      end = nbytes;
      allocated = new_alloc;
      free(buf);
      buf = new_buf;
    } else if (end + data.size() > allocated) {
      // We have enough space, but too much slack at the
      // beginning. Move to compact.
      memmove(buf, buf + start, nbytes);
      start = 0;
      end = nbytes;
    }

    CHECK(end + data.size() <= allocated);

    memcpy(buf + end, data.data(), data.size());
    end += data.size();
  }

 private:
  ContiguousBuffer(const ContiguousBuffer &) = delete;
  ContiguousBuffer(ContiguousBuffer &&) = delete;
  void operator=(const ContiguousBuffer &) = delete;
  void operator=(ContiguousBuffer &&) = delete;

  // Index of the next byte to send.
  size_t start = 0;
  // One past the end of the valid data in buf.
  size_t end = 0;

  size_t allocated = 0;
  // owned. malloc'd.
  uint8_t *buf = nullptr;
};

#endif
