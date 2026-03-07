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
  CHECK(p.OK());
  CHECK(p.Byte() == 0x12);
  CHECK(p.OK());
  CHECK(p.W16() == 0x3456);
  CHECK(p.OK());
  CHECK(p.W24() == 0x789ABC);
  CHECK(p.OK());
  CHECK(p.W32() == 0xDEF01234);
  CHECK(p.empty());
  CHECK(p.OK());
}

static void TestBytesTo() {
  std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04 };
  PacketParser p(data);
  CHECK(p.OK());

  // Consume first byte.
  CHECK(p.Byte() == 0x01);
  CHECK(p.OK());

  uint8_t buffer[4] = {0, 0, 0, 0x2A};
  p.BytesTo(3, buffer);

  CHECK(buffer[0] == 0x02);
  CHECK(buffer[1] == 0x03);
  CHECK(buffer[2] == 0x04);
  CHECK(buffer[3] == 0x2A);
  CHECK(p.empty());
  CHECK(p.OK());
}

static void TestSubpacket() {
  std::vector<uint8_t> data = { 0x77, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
  PacketParser parent(data);
  CHECK(parent.OK());

  CHECK(parent.Byte() == 0x77);
  CHECK(parent.OK());

  PacketParser child = parent.Subpacket(3);
  CHECK(parent.OK());
  CHECK(child.OK());

  CHECK(child.size() == 3);
  CHECK(child.Byte() == 0xAA);
  CHECK(child.Byte() == 0xBB);
  CHECK(child.Byte() == 0xCC);
  CHECK(child.empty());

  // Parent should have skipped over the subpacket.
  CHECK(parent.size() == 2);
  CHECK(parent.Byte() == 0xDD);
  CHECK(parent.Byte() == 0xEE);
  CHECK(parent.OK());
}

static void TestStringViewCtor() {
  PacketParser p("AB");
  CHECK(p.size() == 2);
  CHECK(p.Byte() == 'A');
  CHECK(p.Byte() == 'B');
  CHECK(p.empty());
  CHECK(p.OK());
}

static void TestString() {
  {
    PacketParser p("ABCDEFG");
    CHECK(p.String() == "ABCDEFG");
    CHECK(p.empty());
    CHECK(p.OK());
  }

  {
    PacketParser p("ABCDEFG");
    (void)p.Byte();
    CHECK(p.String(3) == "BCD");
    CHECK(!p.empty());
    CHECK(p.OK());
  }

}

static void TestPrefix() {
  PacketParser p("ABCDEFG");
  CHECK(p.HasPrefix(""));
  CHECK(p.HasPrefix("A"));
  CHECK(p.HasPrefix("ABCDEFG"));
  CHECK(p.OK());
  CHECK(!p.HasPrefix("ABCDEFGH"));
  CHECK(!p.HasPrefix("ABCX"));
  CHECK(!p.HasPrefix("X"));

  CHECK(!p.TryStripPrefix("X"));
  CHECK(p.TryStripPrefix("") && p.size() == 7);
  CHECK(p.OK());
  CHECK(!p.TryStripPrefix("ABCDEFGH") && p.OK());
  CHECK(p.HasPrefix("AB"));
  CHECK(p.TryStripPrefix("AB"));
  CHECK(p.HasPrefix("CD"));
  CHECK(p.String(2) == "CD");
  CHECK(p.size() == 3 && p.OK());
  CHECK(p.String(3) == "EFG");
  CHECK(!p.HasPrefix("X"));
  CHECK(p.HasPrefix(""));
  CHECK(p.TryStripPrefix(""));
  CHECK(p.TryStripPrefix(""));
  CHECK(p.OK());
}

static void TestEquals() {
  PacketParser p("ABCDEFG");
  CHECK(p.Equals("ABCDEFG"));
  CHECK(!p.Equals("ABCDEFGH"));
  CHECK(!p.Equals("ABCDEF"));
  p.Skip(2);
  CHECK(p.Equals("CDEFG"));
}

static void TestSkip() {
  {
    PacketParser p("ABCDEFG");
    p.Skip(4);
    CHECK(p.String(3) == "EFG");
    CHECK(p.empty());
    CHECK(p.OK());
  }

  {
    PacketParser p("ABCDEFG");
    p.Skip(8);
    CHECK(!p.OK());
  }
}

static void TestError() {
  {
    PacketParser p("AB");
    CHECK(p.OK());
    (void)p.W24();
    CHECK(!p.OK());
  }

  {
    PacketParser p("AB");
    CHECK(p.OK());
    (void)p.Byte();
    CHECK(p.OK());
    (void)p.Byte();
    CHECK(p.OK());
    (void)p.Byte();
    CHECK(!p.OK());
  }

  {
    PacketParser p("AB");
    PacketParser c = p.Subpacket(1);
    (void)p.W32();
    CHECK(!p.OK());
    CHECK(c.OK());
    (void)c.W16();
    CHECK(!c.OK());
    PacketParser d = c.Subpacket(1);
    CHECK(!d.OK());
  }

  {
    PacketParser p("AB");
    PacketParser c = p.Subpacket(3);
    CHECK(!p.OK());
    CHECK(!c.OK());
    (void)p.Byte();
    (void)c.Byte();
  }

  {
    PacketParser p("XYZ");
    p.Error();
    CHECK(!p.OK());
  }

  {
    PacketParser p("AB");
    (void)p.String(10);
    (void)p.String();
    CHECK(!p.OK());
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  TestIntegers();
  TestBytesTo();
  TestSubpacket();
  TestStringViewCtor();
  TestString();
  TestPrefix();
  TestEquals();
  TestSkip();

  TestError();

  Print("OK\n");
  return 0;
}
