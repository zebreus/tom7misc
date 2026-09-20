
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"
#include "zip.h"
#include "unicode-data.h"
#include "util.h"

static void Dump() {
  std::unique_ptr<UnicodeData> ud =
    UnicodeData::FromContent(
        ZIP::UnCCZ(Util::ReadFileBytes("../cc-lib/unicode-data.ccz")));

  std::vector<uint32_t> combining;
  for (UnicodeData::CodepointData d : *ud) {
    if (d.category == UnicodeData::Me ||
        d.category == UnicodeData::Mn) {
      combining.push_back(d.codepoint);
    }
  }

  Print("static constexpr size_t NUM_COMBINING = {};\n"
        "namespace {{ extern const std::array<uint32_t, NUM_COMBINING> COMBINING; }}\n\n"
        "namespace {{\n"
        "const std::array<uint32_t, NUM_COMBINING> COMBINING = {{\n"
        "  ",
        combining.size(), combining.size());

  int col = 2;
  for (uint32_t cp : combining) {
    std::string s = std::format("0x{:x},", cp);
    Print("{}", s);
    col += s.size();
    if (col < 68) {
      Print(" ");
    } else {
      col = 2;
      Print("\n  ");
    }
  }
  Print("\n}};\n}}\n");
}


int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 1) << "./combining.exe"
    "Outputs all the codepoints that are 'combining'.\n";

  Dump();

  return 0;
}
