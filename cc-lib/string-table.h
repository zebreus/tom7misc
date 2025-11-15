
// Header-only library for a simple "interned" string table.
// This allows you to store a large number of strings without
// the overhead of individual allocations (and the nul terminator).
// In principle it could also support sharing.
//
// No way to remove strings without discarding the whole table.
//
// Since this would typically be used for cases where you care a lot
// about memory, we support only 32-bit offsets and sizes. You can
// make a 64-bit version or store multiple tables.

#ifndef _CC_LIB_STRING_TABLE_H
#define _CC_LIB_STRING_TABLE_H

#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "base/logging.h"

struct StringTable {
  struct Entry {
    uint32_t offset = 0;
    uint32_t size = 0;
  };

  // Convert an entry into a string view that refers into the table.
  // Note that it is invalidated when you add more strings or finalize.
  std::string_view GetView(const Entry &entry) const {
    return std::string_view((const char *)storage.data() + entry.offset, entry.size);
  }

  // Same, but a byte span.
  std::span<const uint8_t> GetSpan(const Entry &entry) const {
    return std::span<const uint8_t>(storage.data() + entry.offset, entry.size);
  }

  // Invalidates views.
  Entry Add(std::string_view s) {
    CHECK(storage.size() <= (size_t)std::numeric_limits<uint32_t>::max);
    CHECK(s.size() <= (size_t)std::numeric_limits<uint32_t>::max);
    Entry ret{
      .offset = (uint32_t)storage.size(),
      .size = (uint32_t)s.size(),
    };
    for (size_t i = 0; i < s.size(); i++) {
      storage.push_back(s[i]);
    }
    return ret;
  }

  // Invalidates views.
  void Finalize() {
    storage.shrink_to_fit();
  }

 private:
  std::vector<uint8_t> storage;
};

#endif
