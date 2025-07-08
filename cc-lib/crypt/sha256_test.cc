#include "crypt/sha256.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "hexdump.h"
#include "timer.h"

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

  printf("OK\n");
  return 0;
}
