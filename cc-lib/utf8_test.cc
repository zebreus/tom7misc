#include "utf8.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"

using namespace std;

static void TestUnicode() {
  CHECK("*" == UTF8::Encode('*'));
  // Katakana Letter Small Tu
  CHECK("\xE3\x83\x83" == UTF8::Encode(0x30C3));
  // Emoji Banana
  string banana = UTF8::Encode(0x1F34C);
  CHECK("\xF0\x9F\x8D\x8C" == banana) << Util::HexString(banana);

  CHECK(UTF8::Length("banana") == 6);
  for (int i = 1; i < 0x1000; i++) {
    // one possibly multibyte codepoint
    string ss = UTF8::Encode(i);
    // and one ascii character
    ss.push_back('c');
    size_t len = UTF8::Length(ss);
    CHECK(len == 2) << i;
  }


  ArcFour rc("utf8");
  for (int rep = 0; rep < 0x1000; rep++) {
    std::vector<uint32_t> expected;
    uint8_t numc = 1 + (rc.Byte() & 7);

    for (int j = 0; j < numc; j++) {
      uint32_t a = rc.Byte();
      a <<= 8;
      a |= rc.Byte();
      a <<= 8;
      a |= rc.Byte();
      switch (rc.Byte() & 3) {
      default:
      case 0:
        expected.push_back(a & 0x7f);
        break;
      case 1:
        expected.push_back(a & 0x7ff);
        break;
      case 2:
        expected.push_back(a & 0xffff);
        break;
      case 3:
        expected.push_back(a & 0x10ffff);
        break;
      }
    }
    expected.push_back(rep + 1);

    string str;
    for (uint32_t cp : expected)
      str += UTF8::Encode(cp);

    // Now expect the expected.
    std::vector<uint32_t> got = UTF8::Codepoints(str);

    if (expected != got) {
      Print("For codepoint sequence: ");
      for (uint32_t cp : expected) {
        Print("{:04x} ", cp);
      }
      Print("\n");
      Print("Got string: {}\n",
            Util::HexString(str, " "));
      Print("Decoded to: ");
      for (uint32_t cp : got) {
        Print("{:04x} ", cp);
      }
      Print("\n");
      LOG(FATAL) << "Wrong! Rep " << rep;
    }
  }

  // TODO: Test invalid encodings too.
}

static void TestDecoder() {
  std::vector<uint32_t> decoded;
  for (uint32_t cp : UTF8::Decoder("checkmate ♚!")) {
    decoded.push_back(cp);
  }
  CHECK(decoded.size() == 12);
  CHECK(decoded[0] == 'c');
  CHECK(decoded[1] == 'h');
  CHECK(decoded[10] == 0x265A);
  CHECK(decoded[11] == '!');

  {
    auto empty = UTF8::Decoder("");
    CHECK(empty.begin() == empty.end());
  }
}

static void TestCodepoint() {
  CHECK(UTF8::Codepoint("7") == (uint32_t)'7');
  CHECK(UTF8::Codepoint("∃") == 0x2203);
  CHECK(UTF8::Codepoint("♜") == 0x265C);
  CHECK(UTF8::Codepoint("💡") == 0x1F4A1);
}

static void TestParsePrefix() {
  // Test that ParsePrefix does not consume invalid continuation
  // bytes.
  //   E9       : Starts a 3-byte sequence (1110xxxx)
  //   85       : Valid continuation (10xxxxxx)
  //   20       : INVALID continuation (00100000)
  {
    const std::string_view bad = "\xE9\x85\x20";
    CHECK(!UTF8::IsValid(bad));
    auto [len, cp] = UTF8::ParsePrefix(bad.data(), bad.size());
    CHECK(cp == UTF8::INVALID);
    CHECK(len == 1);
  }

  // Same, but with a two-byte sequence.
  {
    std::string_view bad = "\xC3\x20";
    auto [len, cp] = UTF8::ParsePrefix(bad.data(), bad.size());
    CHECK(cp == UTF8::INVALID);
    CHECK(len == 1);
  }

  {
    std::string_view valid = "\xE9\x85\xA0";
    CHECK(UTF8::IsValid(valid));
    auto [len, cp] = UTF8::ParsePrefix(valid.data(), valid.size());
    CHECK(cp == 0x9160);
    CHECK(len == 3);
  }

  // Valid prefix, but then end of string.
  {
    std::string_view truncated = "\xE9\x85";
    CHECK(!UTF8::IsValid(truncated));
    auto [len, cp] = UTF8::ParsePrefix(truncated.data(), truncated.size());
    CHECK(cp == UTF8::INVALID);
    CHECK(len == 1);
  }
}

static void TestConsumePrefix() {
  {
    std::string_view s = "";
    CHECK(UTF8::ConsumePrefix(&s) == UTF8::INVALID && s.empty());
  }

  {
    std::string_view s = "a";
    CHECK(UTF8::ConsumePrefix(&s) == 'a' && s.empty());
  }

  {
    std::string_view s = "♜b";
    CHECK(UTF8::ConsumePrefix(&s) == UTF8::Codepoint("♜") &&
          s == "b");
  }

  {
    std::string_view s = "♜b";
    CHECK(UTF8::ConsumePrefix(&s) == UTF8::Codepoint("♜") &&
          s == "b");
  }

  {
    std::string_view incomplete = "\xF0\x9F\x8D";
    CHECK(UTF8::ConsumePrefix(&incomplete) == UTF8::INVALID &&
          incomplete == "\x9F\x8D");
  }

  {
    std::string_view incomplete = "\xF0\x9F";
    CHECK(UTF8::ConsumePrefix(&incomplete) == UTF8::INVALID &&
          incomplete == "\x9F");
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  TestUnicode();
  TestDecoder();
  TestCodepoint();
  TestParsePrefix();
  TestConsumePrefix();

  Print("OK\n");
  return 0;
}


/* KEEP THIS LINE TOO */
