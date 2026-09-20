
// Needs UnicodeData.txt, e.g. from
// https://www.unicode.org/Public/14.0.0/ucd/UnicodeData.txt

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "unicode-data.h"
#include "utf8.h"
#include "util.h"
#include "zip.h"

static void Dump(uint32_t start, uint32_t end) {
  std::unique_ptr<UnicodeData> ud =
    UnicodeData::FromContent(
        ZIP::UnCCZ(Util::ReadFileBytes("../cc-lib/unicode-data.ccz")));
  std::unordered_map<uint32_t, std::string> names;

  // Now output.
  for (uint32_t codepoint = start; codepoint < end; codepoint++) {
    std::optional<UnicodeData::CodepointData> oc =
      ud->GetByCodepoint(codepoint);

    std::string_view name = oc.has_value() ? oc.value().name : "??";
    Print("  0x{:04x},  // ({}) {}\n",
          codepoint,
          UTF8::Encode(codepoint),
          name);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3) << "./unicoderange.exe 2200 2300\n"
    "Outputs all the unicode codepoints in [U+2200, U+2300).\n";

  uint32_t start = strtol(argv[1], nullptr, 16);
  uint32_t end = strtol(argv[2], nullptr, 16);
  CHECK(start < end && start != 0) << "./unicoderange.exe 2200 2300\n";

  Dump(start, end);

  return 0;
}
