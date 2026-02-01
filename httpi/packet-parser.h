#ifndef _HTTPI_PACKET_PARSER_H
#define _HTTPI_PACKET_PARSER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/logging.h"

// If you read past the end of a packet, the packet is in an error
// state. Reads will continue to work without undefined behavior. You
// should check OK() before declaring success.
struct PacketParser {
  PacketParser(std::span<const uint8_t> payload) :
    original(payload), rest(payload) {
  }

  PacketParser(std::string_view payload) :
    PacketParser(std::span<const uint8_t>{
                     (const uint8_t *)payload.data(),
                     payload.size()}) {
  }

  bool OK() const { return !error; }
  bool empty() const { return rest.empty(); }
  bool Empty() const { return empty(); }
  size_t size() const { return rest.size(); }
  size_t Size() const { return size(); }
  // Force error state.
  void Error() { error = true; }

  // These consume from the head of the packet.
  uint8_t Byte() {
    if (error || rest.empty()) {
      error = true;
      return 0;
    }

    CHECK(!rest.empty());
    const uint8_t b = rest[0];
    rest = rest.last(rest.size() - 1);
    return b;
  }

  uint16_t W16() {
    if (error || rest.size() < 2) {
      error = true;
      return 0;
    }

    CHECK(rest.size() >= 2);
    const uint16_t b1 = rest[0];
    const uint16_t b2 = rest[1];
    rest = rest.last(rest.size() - 2);
    return (b1 << 8) | b2;
  }

  uint32_t W24() {
    if (error || rest.size() < 3) {
      error = true;
      return 0;
    }

    CHECK(rest.size() >= 3);
    const uint32_t b1 = rest[0];
    const uint32_t b2 = rest[1];
    const uint32_t b3 = rest[2];
    rest = rest.last(rest.size() - 3);
    return (b1 << 16) | (b2 << 8) | b3;
  }

  uint32_t W32() {
    if (error || rest.size() < 4) {
      error = true;
      return 0;
    }

    CHECK(rest.size() >= 4);
    const uint32_t b1 = rest[0];
    const uint32_t b2 = rest[1];
    const uint32_t b3 = rest[2];
    const uint32_t b4 = rest[3];
    rest = rest.last(rest.size() - 4);
    return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
  }

  void BytesTo(int num, uint8_t *out) {
    // Don't call memcpy with an invalid pointer
    // (e.g. unallocated vector::data).
    if (num == 0) return;

    if (error || rest.size() < num) {
      error = true;
      memset(out, 0, num);
      return;
    }

    CHECK(out != nullptr) << num;
    CHECK(rest.size() >= num);
    memcpy(out, rest.data(), num);
    rest = rest.last(rest.size() - num);
  }

  // From the remaining payload.
  uint8_t operator [](size_t idx) {
    if (error || idx >= rest.size()) {
      error = true;
      return 0;
    }

    CHECK(idx < rest.size());
    return rest[idx];
  }

  // However, the const version just has to abort if
  // out of bounds.
  uint8_t operator [](size_t idx) const {
    if (error) {
      return 0;
    }

    CHECK(idx < rest.size());
    return rest[idx];
  }

  // Extract and consume the next len bytes.
  // A packet in an error state extracts an error packet.
  PacketParser Subpacket(int len) {
    if (error || rest.size() < len) {
      error = true;
      return *this;
    }

    CHECK(len <= rest.size());
    PacketParser p(rest.first(len));
    rest = rest.last(rest.size() - len);
    return p;
  }

  std::string String(size_t len) {
    std::string ret(len, 0);
    BytesTo(len, (uint8_t*)ret.data());
    return ret;
  }

  // Entire remainder.
  std::string String() {
    return String(size());
  }

  std::vector<uint8_t> Bytes(size_t len) {
    std::vector<uint8_t> ret(len, 0);
    BytesTo(len, ret.data());
    return ret;
  }

  std::vector<uint8_t> Bytes() {
    return Bytes(size());
  }

  bool HasPrefixByte(uint8_t b) const {
    return !error && !rest.empty() && rest[0] == b;
  }

  bool HasPrefix(std::span<const uint8_t> prefix) const {
    if (error || rest.size() < prefix.size())
      return false;

    return SpanEq(prefix, rest.subspan(0, prefix.size()));
  }

  bool HasPrefix(std::string_view prefix) const {
    return HasPrefix(std::span<const uint8_t>((const uint8_t *)prefix.data(),
                                              prefix.size()));
  }

  bool Equals(std::span<const uint8_t> val) const {
    return val.size() == Size() && HasPrefix(val);
  }

  bool Equals(std::string_view str) const {
    return Equals(std::span<const uint8_t>((const uint8_t *)str.data(),
                                           str.size()));
  }

  bool TryStripPrefix(std::span<const uint8_t> prefix) {
    if (HasPrefix(prefix)) {
      Skip(prefix.size());
      return true;
    }

    return false;
  }

  bool TryStripPrefix(std::string_view prefix) {
    return TryStripPrefix(
        std::span<const uint8_t>((const uint8_t *)prefix.data(),
                                 prefix.size()));
  }

  void Skip(size_t num) {
    if (error || rest.size() < num) {
      error = true;
      return;
    }

    rest = rest.last(rest.size() - num);
  }

  std::span<const uint8_t> View() const {
    // Rest is always valid, so we can return it even
    // in an error state.
    return rest;
  }

  const uint8_t *data() const { return rest.data(); }

 private:
  static inline bool SpanEq(std::span<const uint8_t> a,
                            std::span<const uint8_t> b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
      if (a[i] != b[i])
        return false;
    return true;
  }

  // We keep the original payload but it is not currently
  // used.
  std::span<const uint8_t> original;
  std::span<const uint8_t> rest;
  bool error = false;
};

#endif
