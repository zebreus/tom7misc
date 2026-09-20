
#include "unicode-data.h"

#include <memory>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

// a la UnicodeData.txt
static std::string_view SAMPLE_DATA =
  "002E;FULL STOP;Po;0;CS;;;;;N;;;;;\n"
  "0041;LATIN CAPITAL LETTER A;Lu;0;L;;;;;N;;;;0061\n"
  "0042;LATIN CAPITAL LETTER B;Lu;0;L;;;;;N;;;;0062\n"
  "03B1;GREEK SMALL LETTER ALPHA;Ll;0;L;;;;;N;;;;0391\n"
  "215E;VULGAR FRACTION SEVEN EIGHTHS;No;0;ON;<fraction> 0037 2044 0038;;;7/8;N;;;;;\n"
  "2708;AIRPLANE;So;0;ON;;;;;N;;;;;\n";

static void TestUnicodeData() {
  std::unique_ptr<UnicodeData> ud = UnicodeData::FromContent(SAMPLE_DATA);
  CHECK(ud.get() != nullptr);

  auto cp_a = ud->GetByCodepoint(0x41);
  CHECK(cp_a.has_value());
  CHECK(cp_a->codepoint == 0x41);
  CHECK(cp_a->name == "LATIN CAPITAL LETTER A");
  CHECK(cp_a->category == UnicodeData::Lu);

  auto cp_alpha = ud->GetByCodepoint(0x03B1);
  CHECK(cp_alpha.has_value());
  CHECK(cp_alpha->name == "GREEK SMALL LETTER ALPHA");
  CHECK(cp_alpha->category == UnicodeData::Ll);

  auto cp_frac = ud->GetByCodepoint(0x215E);
  CHECK(cp_frac.has_value());
  CHECK(cp_frac->codepoint == 0x215E);
  CHECK(cp_frac->name == "VULGAR FRACTION SEVEN EIGHTHS");
  CHECK(cp_frac->category == UnicodeData::No);

  auto cp_plane = ud->GetByCodepoint(0x2708);
  CHECK(cp_plane.has_value());
  CHECK(cp_plane->category == UnicodeData::So);

  // Not in there.
  CHECK(!ud->GetByCodepoint(0xFFFF).has_value());
  CHECK(!ud->GetByCodepoint(0x0007).has_value());
  CHECK(!ud->GetByCodepoint(0x8000215E).has_value());

  auto name_b = ud->GetByName("LATIN CAPITAL LETTER B");
  CHECK(name_b.has_value());
  CHECK(name_b->codepoint == 0x42);
  CHECK(name_b->name == "LATIN CAPITAL LETTER B");

  CHECK(!ud->GetByName("ALLIGATOR"));
  CHECK(!ud->GetByName("LATIN CAPITAL LETTER").has_value());
  CHECK(!ud->GetByName("LATIN CAPITAL LETTER BX").has_value());
}

static void TestCategories() {
  CHECK(UnicodeData::IsLetter(UnicodeData::Lu));
  CHECK(UnicodeData::IsLetter(UnicodeData::Ll));
  CHECK(UnicodeData::IsLetter(UnicodeData::Lo));
  CHECK(!UnicodeData::IsLetter(UnicodeData::Nd));
  CHECK(!UnicodeData::IsLetter(UnicodeData::Po));

  CHECK(UnicodeData::IsSymbol(UnicodeData::Sm));
  CHECK(UnicodeData::IsSymbol(UnicodeData::So));
  CHECK(!UnicodeData::IsSymbol(UnicodeData::Lu));
  CHECK(!UnicodeData::IsSymbol(UnicodeData::Po));

  CHECK(UnicodeData::IsNumber(UnicodeData::Nd));
  CHECK(UnicodeData::IsNumber(UnicodeData::No));
  CHECK(!UnicodeData::IsNumber(UnicodeData::Lu));

  CHECK(UnicodeData::IsPunctuation(UnicodeData::Po));
  CHECK(UnicodeData::IsPunctuation(UnicodeData::Pc));
  CHECK(!UnicodeData::IsPunctuation(UnicodeData::Sm));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestUnicodeData();
  TestCategories();

  Print("OK\n");
  return 0;
}
