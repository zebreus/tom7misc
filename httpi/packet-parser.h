#ifndef _HTTPI_PACKET_PARSER_H
#define _HTTPI_PACKET_PARSER_H

#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "base/logging.h"

struct PacketParser {
  PacketParser(std::span<const uint8_t> payload) :
    original(payload), rest(payload) {
  }

  PacketParser(std::string_view payload) :
    PacketParser(std::span<const uint8_t>{
                     (const uint8_t *)payload.data(),
                     payload.size()}) {
  }

  bool empty() const { return rest.empty(); }
  size_t size() const { return rest.size(); }

  // These consume from the head of the packet.
  uint8_t Byte() {
    CHECK(!rest.empty());
    const uint8_t b = rest[0];
    rest = rest.last(rest.size() - 1);
    return b;
  }

  uint16_t W16() {
    CHECK(rest.size() >= 2);
    const uint16_t b1 = rest[0];
    const uint16_t b2 = rest[1];
    rest = rest.last(rest.size() - 2);
    return (b1 << 8) | b2;
  }

  uint32_t W24() {
    CHECK(rest.size() >= 3);
    const uint32_t b1 = rest[0];
    const uint32_t b2 = rest[1];
    const uint32_t b3 = rest[2];
    rest = rest.last(rest.size() - 3);
    return (b1 << 16) | (b2 << 8) | b3;
  }

  uint32_t W32() {
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

    CHECK(out != nullptr) << num;
    CHECK(rest.size() >= num);
    memcpy(out, rest.data(), num);
    rest = rest.last(rest.size() - num);
  }

  // From the remaining payload.
  uint8_t operator [](size_t idx) const {
    CHECK(idx < rest.size());
    return rest[idx];
  }

  // Extract and consume the next len bytes.
  PacketParser Subpacket(int len) {
    CHECK(len <= rest.size());
    PacketParser p(rest.first(len));
    rest = rest.last(rest.size() - len);
    return p;
  }

  std::span<const uint8_t> View() const {
    return rest;
  }

  const uint8_t *data() const { return rest.data(); }

 private:
  // We keep the original payload but it is not currently
  // used.
  std::span<const uint8_t> original;
  std::span<const uint8_t> rest;
};

#endif
