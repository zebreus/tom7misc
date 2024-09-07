
// Needs UnicodeData.txt, e.g. from
// https://www.unicode.org/Public/14.0.0/ucd/UnicodeData.txt

#include <cstdio>
#include <cstdint>

#include "util.h"
#include "base/logging.h"
#include "ansi.h"

static void Dump(uint32_t start, uint32_t end) {
  std::unordered_map<uint32_t, std::string> names;

  for (std::string line : Util::NormalizeLines(
           Util::ReadFileToLines("UnicodeData.txt"))) {
    std::vector<std::string> fields = Util::Split(line, ';');
    if (fields.size() >= 2) {
      uint32_t codepoint = strtol(fields[0].c_str(), nullptr, 16);
      names[codepoint] = fields[1];
    }
  }

  // Now output.
  for (uint32_t codepoint = start; codepoint < end; codepoint++) {
    printf("  0x%04x,  // (%s) %s\n",
           codepoint,
           Util::EncodeUTF8(codepoint).c_str(),
           names[codepoint].c_str());
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
