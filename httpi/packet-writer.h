#ifndef _HTTPI_PACKET_WRITER_H
#define _HTTPI_PACKET_WRITER_H

#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "base/logging.h"

struct PacketWriter {
  PacketWriter() {}

  bool empty() const { return payload.empty(); }
  size_t size() const { return payload.size(); }

  void SetW16(size_t idx, uint16_t w) {
    CHECK(idx + 1 < payload.size());
    payload[idx] = (w >> 8) & 0xFF;
    payload[idx + 1] = w & 0xFF;
  }

  void SetW24(size_t idx, uint32_t w) {
    CHECK(idx + 2 < payload.size());
    payload[idx + 0] = (w >> 16) & 0xFF;
    payload[idx + 1] = (w >> 8) & 0xFF;
    payload[idx + 2] = w & 0xFF;
  }

  // These append at the end of the packet.

  void Byte(uint8_t b) {
    payload.push_back(b);
  }

  void W16(uint16_t w) {
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void W24(uint32_t w) {
    payload.push_back((w >> 16) & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void W32(uint32_t w) {
    payload.push_back((w >> 24) & 0xFF);
    payload.push_back((w >> 16) & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void Bytes(std::span<const uint8_t> bs) {
    size_t start = payload.size();
    payload.resize(payload.size() + bs.size());
    memcpy(payload.data() + start, bs.data(), bs.size());
  }

  std::vector<uint8_t> &&Release() && { return std::move(payload); }

 private:
  std::vector<uint8_t> payload;
};

#endif
