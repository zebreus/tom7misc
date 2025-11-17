
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

int main(int argc, char **argv) {
  ANSI::Init();

  TestAlreadyGood();

  Print("OK\n");
  return 0;
}
