#include "packet-writer.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestIntegers() {
  PacketWriter p;
  p.Byte(0x12);
  p.W16(0x3456);
  p.W24(0x789ABC);
  p.W32(0xDEF01234);
  p.W64(uint64_t{0x56789ABCDEF01234});

  std::vector<uint8_t> expected = {
    0x12,
    0x34, 0x56,
    0x78, 0x9A, 0xBC,
    0xDE, 0xF0, 0x12, 0x34,
    0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34,
  };

  CHECK(p.size() == expected.size());
  for (size_t i = 0; i < p.size(); i++) {
    CHECK(p.View()[i] == expected[i]) << "Mismatch at index " << i;
  }

  std::vector<uint8_t> actual = std::move(p).Release();
  CHECK(actual == expected);
}

static void TestDelayedLength() {
  PacketWriter p;
  {
    p.Byte(0x2A);
    auto len = p.Length16();
    p.W16(0xCAFE);
    p.Byte(0xBB);
    len.Fill();
  }

  uint8_t expected_len = 0x03;
  std::vector<uint8_t> expected = {
    0x2A, 0x00, expected_len, 0xCA, 0xFE, 0xBB
  };

  CHECK(p.size() == expected.size());
  for (size_t i = 0; i < p.size(); i++) {
    CHECK(p.View()[i] == expected[i]);
  }

  std::vector<uint8_t> actual = std::move(p).Release();
  CHECK(actual == expected);
}

static void TestIntegersLE() {
  PacketWriter p;
  p.Byte(0x12);
  p.W16LE(0x3456);
  p.W24LE(0x789ABC);
  p.W32LE(0xDEF01234);
  p.W64LE(uint64_t{0x56789ABCDEF01234});

  std::vector<uint8_t> expected = {
    0x12,
    0x56, 0x34,
    0xBC, 0x9A, 0x78,
    0x34, 0x12, 0xF0, 0xDE,
    0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56,
  };

  CHECK(p.size() == expected.size());
  for (size_t i = 0; i < p.size(); i++) {
    CHECK(p.View()[i] == expected[i]) << "Mismatch at index " << i;
  }

  std::vector<uint8_t> actual = std::move(p).Release();
  CHECK(actual == expected);
}

static void TestDelayedLengthLE() {
  PacketWriter p;
  {
    p.Byte(0x2A);
    auto len = p.Length16LE();
    p.W16LE(0xCAFE);
    p.Byte(0xBB);
    len.Fill();
  }

  uint8_t expected_len = 0x03;
  std::vector<uint8_t> expected = {
    0x2A, expected_len, 0x00, 0xFE, 0xCA, 0xBB
  };

  CHECK(p.size() == expected.size());
  for (size_t i = 0; i < p.size(); i++) {
    CHECK(p.View()[i] == expected[i]);
  }

  std::vector<uint8_t> actual = std::move(p).Release();
  CHECK(actual == expected);
}

static void TestAliasing() {
  PacketWriter p;
  // We want the PacketWriter's underlying vector to not have capacity
  // to double in size. There doesn't seem to be a way to actually
  // guarantee this with std::vector; oh well.
  p.reserve(4);
  p.W32(0x11223344);

  // Append from its own interior. This tests that we handle aliasing.
  std::span<const uint8_t> view = p.View();
  p.Bytes(view);

  std::vector<uint8_t> expected = {
    0x11, 0x22, 0x33, 0x44,
    0x11, 0x22, 0x33, 0x44,
  };

  CHECK(p.size() == expected.size());
  for (size_t i = 0; i < p.size(); i++) {
    CHECK(p.View()[i] == expected[i]) << "Failed at " << i;
  }

  std::vector<uint8_t> actual = std::move(p).Release();
  CHECK(actual == expected);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestIntegers();
  TestDelayedLength();
  TestAliasing();

  TestIntegersLE();
  TestDelayedLengthLE();

  Print("OK\n");
  return 0;
}
