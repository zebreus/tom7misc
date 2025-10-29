
#include "crypt/sha512.h"

#include <cstdio>
#include <string_view>
#include <vector>
#include <cstdint>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "hexdump.h"
#include "timer.h"

static void TestExpected(int line,
                         const std::vector<uint8_t> &digest,
                         std::string_view expected_ascii) {
  CHECK(digest.size() == SHA512::DIGEST_LENGTH);
  std::string asc = SHA512::Ascii(digest);
  CHECK(asc.size() == SHA512::DIGEST_LENGTH * 2);
  if (asc != expected_ascii) {
    printf("Test at line: %d\n", line);
    printf("Digest bytes:\n%s\n", HexDump::Color(digest).c_str());
    printf("As ASCII:\n%s\n", asc.c_str());
    printf("Expected bytes:\n%s\n", std::string(expected_ascii).c_str());

    LOG(FATAL) << "Mismatch!";
  }
}

#define TEST(sha, expected) TestExpected(__LINE__, sha, expected)

static void TestKnown() {
  TEST(SHA512::HashString("abc"),
       "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9"
       "eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d44"
       "23643ce80e2a9ac94fa54ca49f");

  std::vector<uint8_t> empty;
  TEST(SHA512::HashSpan(empty),
       "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc"
       "83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f"
       "63b931bd47417a81a538327af927da3e");

  Timer timer;
  SHA512::Ctx ctx;
  SHA512::Init(&ctx);
  for (int i = 0; i < 16777216; i++) {
    SHA512::UpdateString(
        &ctx,
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno");
  }
  std::vector<uint8_t> digest = SHA512::FinalVector(&ctx);
  TEST(digest,
       "b47c933421ea2db149ad6e10fce6c7f93d0752380180ffd7"
       "f4629a712134831d77be6091b819ed352c2967a2e2d4fa50"
       "50723c9630691f1a05a7281dbe6c1086");
  printf("Digested 1GB in %s\n", ANSI::Time(timer.Seconds()).c_str());
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestKnown();

  printf("OK\n");
  return 0;
}
