
#include "html-entities.h"

#include <map>
#include <string>
#include <format>

#include "base/logging.h"
#include "ansi.h"
#include "base/print.h"
#include "re2/re2.h"

static void Fixup() {

  auto m = HTMLEntities::GetMap();

  RE2 one("##([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])");
  RE2 two("##([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])"
          "##([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])");

  std::map<std::string, std::string> out;
  for (const auto &[k, v] : m) {
    uint32_t w1, w2;
    if (RE2::FullMatch(v, one, RE2::Hex(&w1))) {
      if (w1 >= 0xD800 && w1 <= 0xDFFF) {
        LOG(FATAL) << "Found an unpaired surrogate code unit: " << k;
      }
      out[k] = std::format("\\u{:04x}", w1);
    } else if (RE2::FullMatch(v, two, RE2::Hex(&w1), RE2::Hex(&w2))) {
      if (w1 >= 0xD800 && w1 <= 0xDBFF &&
          w2 >= 0xDC00 && w2 <= 0xDFFF) {
        uint32_t ww = 0x10000 + ((w1 - 0xD800) << 10 | (w2 - 0xDC00));
        out[k] = std::format("\\U{:08x}", ww);
      } else {
        out[k] = std::format("\\u{:04x}\\u{:04x}", w1, w2);
      }
    } else {
      LOG(FATAL) << "Expected a one or two valid codepoints, or a surrogate pair: "
                 << k;
    }
  }

  for (const auto &[k, v] : out) {
    Print("  {{ \"{}\", \"{}\" }},\n", k, v);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Fixup();

  Print("OK\n");
  return 0;
}
