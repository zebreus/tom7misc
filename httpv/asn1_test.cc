#include "asn1.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big.h"
#include "hexdump.h"

static std::vector<uint8_t> V(const std::initializer_list<uint8_t> &v) {
  return std::vector<uint8_t>(v);
}

#define CHECK_VEQ(a, b) do {                                \
    auto aa = std::vector<uint8_t>(a);                      \
    auto bb = std::vector<uint8_t>(b);                      \
    CHECK(aa == bb) << "Expected equal:\n" << #a <<         \
      "\nwhich is\n" << HexDump::Color(aa) <<               \
      "\nand\n" << #b <<                                    \
      "\nwhich is\n" << HexDump::Color(bb) << "\n";         \
  } while (0)

static void TestInt() {
  CHECK_VEQ(ASN1::EncodeInt(BigInt(0)), V({0x02, 0x01, 0x00}));
  CHECK_VEQ(ASN1::EncodeInt(BigInt(127)), V({0x02, 0x01, 0x7F}));
  CHECK_VEQ(ASN1::EncodeInt(BigInt(128)), V({0x02, 0x02, 0x00, 0x80}));
  CHECK_VEQ(ASN1::EncodeInt(BigInt(255)), V({0x02, 0x02, 0x00, 0xFF}));
  CHECK_VEQ(ASN1::EncodeInt(BigInt(256)), V({0x02, 0x02, 0x01, 0x00}));
}

static void TestLength() {
  std::vector<uint8_t> ten_bytes(10, 0xFF);
  std::vector<uint8_t> seq_short = ASN1::EncodeSequence(ten_bytes);
  CHECK(seq_short[0] == 0x30);
  CHECK(seq_short[1] == 0x0A);

  std::vector<uint8_t> large_bytes(128, 0xFF);
  std::vector<uint8_t> seq_long = ASN1::EncodeSequence(large_bytes);
  CHECK(seq_long[0] == 0x30);
  CHECK(seq_long[1] == 0x81);
  CHECK(seq_long[2] == 0x80);
}

static void TestStrings() {
  CHECK_VEQ(ASN1::EncodeUTF8String("f*o"),
            V({0x0C, 0x03, 0x66, 0x2A, 0x6F}));
  CHECK_VEQ(ASN1::EncodeIA5String("b*r"),
            V({0x16, 0x03, 0x62, 0x2A, 0x72}));
}

static void TestBitString() {
  std::vector<uint8_t> bits = {0xFF};
  CHECK_VEQ(ASN1::EncodeBitString(bits, 4),
            V({0x03, 0x02, 0x04, 0xF0}));
}

int main() {
  ANSI::Init();

  TestInt();
  TestLength();

  TestStrings();

  TestBitString();

  Print("OK\n");
  return 0;
}
