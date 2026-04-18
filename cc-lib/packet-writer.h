// Utility for populating an in-memory buffer of binary data,
// e.g. for network or file formats.
//
// This is conceptually a wrapper around a vector of bytes
// with some utilities. The buffer is owned by the object,
// and can grow as the packet is assembled.

#ifndef _CC_LIB_PACKET_WRITER_H
#define _CC_LIB_PACKET_WRITER_H

#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"

struct PacketWriter {
  PacketWriter() {}

  bool empty() const { return payload.empty(); }
  size_t size() const { return payload.size(); }
  void reserve(size_t n) { payload.reserve(n); }

  // Words are network byte order (big-endian) by default.

  void SetW8(size_t idx, uint8_t b) {
    CHECK(idx < payload.size());
    payload[idx] = b;
  }

  void SetW8LE(size_t idx, uint8_t b) {
    SetW8(idx, b);
  }


  void SetW16(size_t idx, uint16_t w) {
    CHECK(idx + 1 < payload.size());
    payload[idx] = (w >> 8) & 0xFF;
    payload[idx + 1] = w & 0xFF;
  }

  void SetW16LE(size_t idx, uint16_t w) {
    CHECK(idx + 1 < payload.size());
    payload[idx] = w & 0xFF;
    payload[idx + 1] = (w >> 8) & 0xFF;
  }


  void SetW24(size_t idx, uint32_t w) {
    CHECK(idx + 2 < payload.size());
    payload[idx + 0] = (w >> 16) & 0xFF;
    payload[idx + 1] = (w >> 8) & 0xFF;
    payload[idx + 2] = w & 0xFF;
  }

  void SetW24LE(size_t idx, uint32_t w) {
    CHECK(idx + 2 < payload.size());
    payload[idx + 0] = w & 0xFF;
    payload[idx + 1] = (w >> 8) & 0xFF;
    payload[idx + 2] = (w >> 16) & 0xFF;
  }


  void SetW32(size_t idx, uint32_t w) {
    CHECK(idx + 3 < payload.size());
    payload[idx + 0] = (w >> 24) & 0xFF;
    payload[idx + 1] = (w >> 16) & 0xFF;
    payload[idx + 2] = (w >> 8) & 0xFF;
    payload[idx + 3] = w & 0xFF;
  }

  void SetW32LE(size_t idx, uint32_t w) {
    CHECK(idx + 3 < payload.size());
    payload[idx + 0] = w & 0xFF;
    payload[idx + 1] = (w >> 8) & 0xFF;
    payload[idx + 2] = (w >> 16) & 0xFF;
    payload[idx + 3] = (w >> 24) & 0xFF;
  }

  // These append at the end of the packet.

  void Byte(uint8_t b) {
    payload.push_back(b);
  }

  void W8(uint8_t b) { Byte(b); }
  void W8LE(uint8_t b) { W8(b); }


  void W16(uint16_t w) {
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void W16LE(uint16_t w) {
    payload.push_back(w & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
  }


  void W24(uint32_t w) {
    payload.push_back((w >> 16) & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void W24LE(uint32_t w) {
    payload.push_back(w & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back((w >> 16) & 0xFF);
  }


  void W32(uint32_t w) {
    payload.push_back((w >> 24) & 0xFF);
    payload.push_back((w >> 16) & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back(w & 0xFF);
  }

  void W32LE(uint32_t w) {
    payload.push_back(w & 0xFF);
    payload.push_back((w >> 8) & 0xFF);
    payload.push_back((w >> 16) & 0xFF);
    payload.push_back((w >> 24) & 0xFF);
  }


  void W64(uint64_t w) {
    for (int i = 7; i >= 0; i--) {
      payload.push_back((w >> (i * 8)) & 0xFF);
    }
  }

  void W64LE(uint64_t w) {
    for (int i = 0; i < 8; i++) {
      payload.push_back((w >> (i * 8)) & 0xFF);
    }
  }


  void Bytes(std::span<const uint8_t> bs) {
    // Avoid calling memcpy with possibly null data.
    if (bs.empty()) return;
    if (bs.data() >= payload.data() &&
        bs.data() < payload.data() + payload.size()) [[unlikely]] {
      // The argument is from the packet itself; we need to
      // copy in this case to avoid undefined behavior.
      std::vector<uint8_t> copy(bs.begin(), bs.end());
      Bytes(copy);
      return;
    }
    std::span buf = Buf(bs.size());
    memcpy(buf.data(), bs.data(), bs.size());
  }

  void Bytes(std::string_view str) {
    Bytes(std::span<const uint8_t>((const uint8_t*)str.data(), str.size()));
  }

  // Append uninitialized space of the given size and return a view of
  // it so that the caller can fill it in. You should fill it
  // immediately; other operations invalidate the view.
  std::span<uint8_t> Buf(size_t size) {
    size_t old_size = payload.size();
    payload.resize(payload.size() + size);
    return std::span<uint8_t>(payload.data() + old_size, size);
  }

  template<size_t B, bool BIG_ENDIAN>
  struct DelayedLength {
    static_assert(B >= 1 && B <= 3);
    DelayedLength(PacketWriter *p) :
      packet(p), length_pos(p->size()) {
      for (size_t i = 0; i < B; i++) {
        packet->Byte(0);
      }
    }

    // Fill the length, assuming the content runs from the
    // position after the length to the current position.
    void Fill() {
      uint32_t len = packet->size() - (length_pos + B);
      CHECK(length_pos + B <= packet->size()) << "Bug: We already "
        "made space for the length in the ctor?";

      if constexpr (B == 1) {
        if constexpr (BIG_ENDIAN) {
          packet->SetW8(length_pos, len);
        } else {
          packet->SetW8LE(length_pos, len);
        }
      } else if constexpr (B == 2) {
        if constexpr (BIG_ENDIAN) {
          packet->SetW16(length_pos, len);
        } else {
          packet->SetW16LE(length_pos, len);
        }
      } else if constexpr (B == 3) {
        if constexpr (BIG_ENDIAN) {
          packet->SetW24(length_pos, len);
        } else {
          packet->SetW24LE(length_pos, len);
        }
      }
    }

    // XXX Consider aborting in destructor if never
    // Filled? Or filling automatically?

   private:
    PacketWriter *packet = nullptr;
    size_t length_pos = 0;
  };

  DelayedLength<3, true> Length24() {
    return DelayedLength<3, true>(this);
  }

  DelayedLength<3, false> Length24LE() {
    return DelayedLength<3, false>(this);
  }


  DelayedLength<2, true> Length16() {
    return DelayedLength<2, true>(this);
  }

  DelayedLength<2, false> Length16LE() {
    return DelayedLength<2, false>(this);
  }


  DelayedLength<1, true> Length8() {
    return DelayedLength<1, true>(this);
  }

  DelayedLength<1, false> Length8LE() {
    return DelayedLength<1, false>(this);
  }

  std::span<const uint8_t> View() const { return payload; }
  std::span<uint8_t> MutableView() { return std::span(payload); }
  uint8_t *data() { return payload.data(); }
  std::vector<uint8_t> &&Release() && { return std::move(payload); }

 private:
  std::vector<uint8_t> payload;
};

#endif
