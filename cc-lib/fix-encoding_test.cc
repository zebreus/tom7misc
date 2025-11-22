
#include "fix-encoding.h"

#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#include "utf8.h"

#define ALREADY_GOOD(str) do {                       \
    std::string s = (str);                           \
    std::string fixed = FixEncoding::Fix(s);         \
    CHECK(s == fixed) << "Expected the string [" <<  \
      s << "] (" << #str << ") to be unchanged " <<  \
      "by Fix. Got:\n[" << fixed << "]\n";           \
  } while (false)

static void TestAlreadyGood() {
  ALREADY_GOOD("");
  ALREADY_GOOD("*");
  // Katakana Letter Small Tu
  ALREADY_GOOD(UTF8::Encode(0x303C));
  ALREADY_GOOD(UTF8::Encode(0x1F34C));
  ALREADY_GOOD("𝕋𝕠𝕞 𝟟");
  ALREADY_GOOD("(っ◔◡◔)っ");
  ALREADY_GOOD("Seven Bridges of Königsberg");
  ALREADY_GOOD("¿Qué?");
  ALREADY_GOOD("A ⋁ ¬A");
}

static void ExpectedNotBad() {
  CHECK(!FixEncoding::IsBad("Más café, por favor."));
  CHECK(!FixEncoding::IsBad("Voilà"));
  CHECK(!FixEncoding::IsBad("100°C"));
  CHECK(!FixEncoding::IsBad("£500"));
  CHECK(!FixEncoding::IsBad("Ελλάδα"));
  CHECK(!FixEncoding::IsBad("Россия"));
}

static void ExpectedBad() {
  // Pattern: Ã[\u00a0¡]
  // This is mojibake for "à" (C3 A0) -> "Ã" + NBSP.
  // Makes sure that we are using the UTF-8 encoding of U+00A0, not the byte \xa0.
  CHECK(FixEncoding::IsBad("Ã\u00a0"));

  // Common Windows-1252 2-char mojibake
  // Mojibake for "é" (C3 A9) -> "Ã" + "©"
  CHECK(FixEncoding::IsBad("Ã©"));

  // Mojibake for "í" (C3 AD) -> "Ã" + Soft Hyphen
  CHECK(FixEncoding::IsBad("Ã\u00ad"));

  // C1 Control Characters
  // These are almost never intended in valid text.
  CHECK(FixEncoding::IsBad("Test\u0080Case"));

  // "â" (Lower Accented) + "–" (Box/Punctuation range)
  CHECK(FixEncoding::IsBad("â│"));

  // Windows-1252 encodings of 'à' and 'á' with context
  // Mojibake for "fácil" -> "fÃ cil"
  CHECK(FixEncoding::IsBad("fÃ cil"));

  CHECK(FixEncoding::IsBad("β€®"));

  // "â‚¬" is the UTF-8 encoding of €, interpreted as Windows-1252.
  CHECK(FixEncoding::IsBad("â‚¬"));

  CHECK(FixEncoding::IsBad("Ã§"));

  // Make sure that regex ranges [a-b] between UTF-8-encoded codepoints
  // work correctly.
  CHECK(FixEncoding::IsBad("│Õ"));
  CHECK(FixEncoding::IsBad("xλ¬"));
}

static void TestVariantDecode() {
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("\xC0\x80"), std::string("\0", 1));
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("null \xC0\x80!"), std::string("null \0!", 6));

  // Surrogate pair.
  CHECK_EQ(FixEncoding::DecodeVariantUTF8(
               // U+D83D
               "\xED\xA0\xBD"
               // U+DCA9
               "\xED\xB2\xA9"), "💩");
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("PO" "\xED\xA0\xBD\xED\xB2\xA9" "P"),
           "PO💩P");

  // Incomplete surrogate pairs
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("\xED\xA0\xBD"), "\xED\xA0\xBD");
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("\xED\xA0\xBDx"), "\xED\xA0\xBDx");
  CHECK_EQ(FixEncoding::DecodeVariantUTF8("\xED\xA0\xBD\xED\xA0\xBD"),
           "\xED\xA0\xBD\xED\xA0\xBD");


  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0\x81").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0\x00").has_value());

  CHECK(!FixEncoding::DecodeVariantUTF8("\xFF").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\x80").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xE0\x80").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xED\xA0").has_value());
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestAlreadyGood();
  ExpectedNotBad();
  ExpectedBad();
  TestVariantDecode();

  Print("OK\n");
  return 0;
}
