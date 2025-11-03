#include "crypt/sha256.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "hexdump.h"
#include "timer.h"

#define CHECK_SPAN_EQ(a, b) do {                \
    auto aa = std::span<const uint8_t>(a);      \
    auto bb = std::span<const uint8_t>(b);      \
    CHECK(aa.size() == bb.size());              \
    for (size_t i = 0; i < aa.size(); i++) {    \
      CHECK(aa[i] == bb[i]);                    \
    }                                           \
  } while (0)

static void TestAscii() {
  std::vector<uint8_t> out;
  CHECK(!SHA256::UnAscii("wrong length", &out));

  std::vector<uint8_t> raw = SHA256::HashString("coffee");
  CHECK(raw.size() == SHA256::DIGEST_LENGTH);
  std::string asc = SHA256::Ascii(raw);
  CHECK(asc.size() == SHA256::DIGEST_LENGTH * 2);
  CHECK(SHA256::UnAscii(asc, &out));
  CHECK(out.size() == SHA256::DIGEST_LENGTH);

  CHECK(raw == out) << "From ascii:\n" << asc
                    << "Raw:\n" << HexDump::Color(raw)
                    << "Out:\n" << HexDump::Color(out);
}

static void TestKnown() {

  CHECK(SHA256::Ascii(SHA256::HashString("")) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  CHECK(SHA256::Ascii(SHA256::HashString("ponycorn")) ==
        "6be4ca413e5958953587238dda80aa399b03f1ad7a61ea681d53e1f69e6daa15");

  CHECK(SHA256::Ascii(SHA256::HashString(
                          "abcdbcdecdefdefgefghfghighijhijkijkljkl"
                          "mklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  Timer timer;
  SHA256::Ctx ctx;
  SHA256::Init(&ctx);
  for (int i = 0; i < 16777216; i++) {
    SHA256::UpdateString(
        &ctx,
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno");
  }
  std::vector<uint8_t> digest = SHA256::FinalVector(&ctx);
  CHECK(SHA256::Ascii(digest) ==
        "50e72a0e26442fe2552dc3938ac58658228c0cbfb1d2ca872ae435266fcd055e");
  printf("Digested 1GB in %s\n", ANSI::Time(timer.Seconds()).c_str());

}

static std::vector<uint8_t> StringVec(std::string_view v) {
  std::vector<uint8_t> ret(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    ret[i] = v[i];
  }
  return ret;
}

static void TestHMAC() {
  {
    std::vector<uint8_t> key(20, 0x0b);
    std::vector<uint8_t> message = {
      0x48, 0x69, 0x20, 0x54, 0x68, 0x65, 0x72, 0x65,
    };
    std::vector<uint8_t> expected = {
      0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
      0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
      0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
      0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
    };

    std::array<uint8_t, 32> actual =
      SHA256::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }

  {
    std::vector<uint8_t> key = StringVec("Jefe");
    std::vector<uint8_t> message = StringVec(
        "what do ya want for nothing?");

    std::vector<uint8_t> expected = {
      0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
      0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
      0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
      0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43,
    };

    std::array<uint8_t, 32> actual =
      SHA256::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }

  {
    std::vector<uint8_t> key(131, 0xaa);
    std::vector<uint8_t> message = StringVec(
        "This is a test using a larger than block-size key "
        "and a larger than block-size data. The key needs "
        "to be hashed before being used by the HMAC "
        "algorithm.");

    std::vector<uint8_t> expected = {
      0x9b, 0x09, 0xff, 0xa7, 0x1b, 0x94, 0x2f, 0xcb,
      0x27, 0x63, 0x5f, 0xbc, 0xd5, 0xb0, 0xe9, 0x44,
      0xbf, 0xdc, 0x63, 0x64, 0x4f, 0x07, 0x13, 0x93,
      0x8a, 0x7f, 0x51, 0x53, 0x5c, 0x3a, 0x35, 0xe2,
    };

    std::array<uint8_t, 32> actual =
      SHA256::HMAC(key, message);

    CHECK_SPAN_EQ(actual, expected);
  }

  Print("HMAC OK.\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestKnown();

  {
    std::vector<uint8_t> vec;
    const std::string s = "lorem ipsum dolor SIT AMET";
    for (char c : s) vec.push_back((uint8_t)c);
    CHECK(SHA256::HashString(s) == SHA256::HashVector(vec));
  }

  CHECK(SHA256::HashPtr("ponycorn", 8) == SHA256::HashString("ponycorn"));

  CHECK(SHA256::HashPtr("ponycorn", 8) == SHA256::HashStringView("ponycorn"));

  std::vector<uint8_t> pony;
  for (int i = 0; i < 8; i++) pony.push_back("ponycorn"[i]);
  CHECK(SHA256::HashString("ponycorn") == SHA256::HashSpan(pony));
  CHECK(SHA256::HashString("ponycorn") == SHA256::HashVector(pony));

  {
    SHA256::Ctx ctx;
    SHA256::Init(&ctx);
    SHA256::Update(&ctx, (const uint8_t*)"p", 1);
    SHA256::UpdateString(&ctx, "ony");
    std::vector<uint8_t> corn = {'c', 'o', 'r', 'n'};
    SHA256::UpdateSpan(&ctx, corn);
    std::vector<uint8_t> res = SHA256::FinalVector(&ctx);
    CHECK(res == SHA256::HashStringView("ponycorn"));
  }

  TestAscii();
  TestHMAC();

  Print("OK\n");
  return 0;
}
