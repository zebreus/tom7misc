
#include "unicode-data.h"

#include <memory>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

// a la UnicodeData.txt
static std::string_view SAMPLE_DATA =
  "0041;LATIN CAPITAL LETTER A;Lu;0;L;;;;;N;;;;0061\n"
  "0042;LATIN CAPITAL LETTER B;Lu;0;L;;;;;N;;;;0062\n"
  "215E;VULGAR FRACTION SEVEN EIGHTHS;No;0;ON;<fraction> 0037 2044 0038;;;7/8;N;;;;;\n";

static void TestUnicodeData() {
  std::unique_ptr<UnicodeData> ud = UnicodeData::FromContent(SAMPLE_DATA);
  CHECK(ud.get() != nullptr);

  auto cp_a = ud->GetByName(0x41);
  CHECK(cp_a.has_value());
  CHECK(cp_a->codepoint == 0x41);
  CHECK(cp_a->name == "LATIN CAPITAL LETTER A");

  auto cp_frac = ud->GetByName(0x215E);
  CHECK(cp_frac.has_value());
  CHECK(cp_frac->codepoint == 0x215E);
  CHECK(cp_frac->name == "VULGAR FRACTION SEVEN EIGHTHS");

  // Not in there.
  CHECK(!ud->GetByName(0xFFFF).has_value());
  CHECK(!ud->GetByName(0x0007).has_value());
  CHECK(!ud->GetByName(0x8000215E).has_value());

  auto name_b = ud->GetByName("LATIN CAPITAL LETTER B");
  CHECK(name_b.has_value());
  CHECK(name_b->codepoint == 0x42);
  CHECK(name_b->name == "LATIN CAPITAL LETTER B");

  CHECK(!ud->GetByName("ALLIGATOR"));
  CHECK(!ud->GetByName("LATIN CAPITAL LETTER").has_value());
  CHECK(!ud->GetByName("LATIN CAPITAL LETTER BX").has_value());
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestUnicodeData();

  Print("OK\n");
  return 0;
}
