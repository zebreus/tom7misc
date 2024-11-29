
#include <cstdio>
#include <functional>

#include "base/logging.h"
#include "util.h"
#include "um.h"

using string = std::string;

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./extract-codex.exe codex.umz";

  string s = "(\\b.bb)(\\v.vv)06FHPVboundvarHRAk" "p";
  int sidx = 0;
  auto GetChar = [&s, &sidx]() -> int {
      if (sidx >= (int)s.size()) return -1;
      else return s[sidx++];
    };

  static constexpr int SKIP_BYTES = 192;
  std::vector<uint8_t> out;
  auto PutChar = [&out](uint8_t c) {
      if ((int)out.size() < SKIP_BYTES) {
        fprintf(stderr, "%c", c);
        fflush(stderr);
      }
      out.push_back(c);
    };

  std::vector<uint8_t> pgm = Util::ReadFileBytes(argv[1]);
  CHECK(!pgm.empty()) << argv[1];

  UM um(pgm);

  um.Run(GetChar, PutChar);

  out.erase(out.begin(), out.begin() + SKIP_BYTES);

  Util::WriteFileBytes("codex.um", out);
  printf("Wrote %d bytes.\n", (int)out.size());
  return 0;
}
