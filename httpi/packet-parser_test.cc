#include "packet-parser.h"

#include <vector>
#include <cstdint>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestIntegers() {
  std::vector<uint8_t> data = {
    0x12,
    0x34, 0x56,
    0x78, 0x9A, 0xBC,
    0xDE, 0xF0, 0x12, 0x34
  };

  PacketParser p(data);
  CHECK(p.Byte() == 0x12);
  CHECK(p.W16() == 0x3456);
  CHECK(p.W24() == 0x789ABC);
  CHECK(p.W32() == 0xDEF01234);
  CHECK(p.empty());
}

static void TestBytesTo() {
  std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04 };
  PacketParser p(data);

  // Consume first byte.
  CHECK(p.Byte() == 0x01);

  uint8_t buffer[4] = {0, 0, 0, 0x2A};
  p.BytesTo(3, buffer);

  CHECK(buffer[0] == 0x02);
  CHECK(buffer[1] == 0x03);
  CHECK(buffer[2] == 0x04);
  CHECK(buffer[3] == 0x2A);
  CHECK(p.empty());
}

static void TestSubpacket() {
  std::vector<uint8_t> data = { 0x77, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
  PacketParser parent(data);

  CHECK(parent.Byte() == 0x77);

  PacketParser child = parent.Subpacket(3);

  CHECK(child.size() == 3);
  CHECK(child.Byte() == 0xAA);
  CHECK(child.Byte() == 0xBB);
  CHECK(child.Byte() == 0xCC);
  CHECK(child.empty());

  // Parent should have skipped over the subpacket.
  CHECK(parent.size() == 2);
  CHECK(parent.Byte() == 0xDD);
  CHECK(parent.Byte() == 0xEE);
}

static void TestStringView() {
  PacketParser p("AB");
  CHECK(p.size() == 2);
  CHECK(p.Byte() == 'A');
  CHECK(p.Byte() == 'B');
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestIntegers();
  TestBytesTo();
  TestSubpacket();
  TestStringView();

  Print("OK\n");
  return 0;
}
