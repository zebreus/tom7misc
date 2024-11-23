
#ifndef _DTAS_ZONING_H
#define _DTAS_ZONING_H

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

#include "util.h"
#include "base/logging.h"

// Assertions about what address ranges are for.
// For example, for approximate analyses, we can
// assert that some region that is known to contain
// data is not executed.
struct Zoning {
  // Executable bit. This describes the address of the
  // first byte of the opcode. In conservating zoning,
  // a multi-byte instruction has X on its first byte
  // but not on its later bytes.
  static inline constexpr uint8_t X = 0b10000000;
  // TODO: More bits as necessary.

  // One byte per address 0000-FFFF.
  std::vector<uint8_t> addr;

  // Default zoning marks everything executable.
  Zoning() : addr(65536, X) {}
  Zoning(std::vector<uint8_t> a) : addr(std::move(a)) {}

  // Mark nothing.
  void Clear() {
    for (uint8_t &z : addr) z = 0;
  }

  void Save(const std::string &filename) const {
    Util::WriteFileBytes(filename, addr);
  }

  static Zoning FromFile(const std::string &filename) {
    std::vector<uint8_t> v = Util::ReadFileBytes(filename);
    // Missing file ok.
    if (v.empty()) return Zoning();

    CHECK(v.size() == 65536) << "Bad zoning file: " << filename;

    return Zoning(std::move(v));
  }
};

#endif
