
#include "unicode-data.h"

#include <memory>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "re2-util.h"
#include "re2/re2.h"
#include "util.h"
#include "zip.h"

static void Filter(std::string_view cclib) {
  std::string datafile = Util::DirPlus(cclib, "unicode-data.ccz");
  std::unique_ptr<UnicodeData> unicode =
    UnicodeData::FromContent(ZIP::UnCCZ(Util::ReadFileBytes(datafile)));
  CHECK(unicode.get() != nullptr) << datafile;

  std::string input = Util::ReadStdin();

  RE2 re("\"\\\\N{([-A-Z0-9 ]+)}\"");

  std::string output =
    RE2Util::MapReplacement(
        input, re,
        [&unicode](std::span<const std::string_view> matches) {
          if (std::optional<UnicodeData::CodepointData> co =
              unicode->GetByName(matches[1])) {
            uint32_t cp = co.value().codepoint;
            std::string cs =
              cp <= 0xFFFF ?
              std::format("\\u{:04x}", cp) :
              std::format("\\U{:08x}", cp);

            return std::format("\"{}\"  // {}", cs, matches[1]);

          } else {
            Print("Couldn't find '{}'\n", matches[1]);
            return std::string(matches[0]);
          }
        });

  Print("{}", output);
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string program_dir = Util::BinaryDir(argv[0]);
  std::string cclib = Util::DirPlus(program_dir, "../cc-lib");

  Filter(cclib);

  return 0;
}
