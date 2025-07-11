
// Randomize the order of lines in a file, very securely!

#include <vector>
#include <string>

#include "arcfour.h"
#include "base/logging.h"
#include "crypt/cryptrand.h"
#include "randutil.h"
#include "util.h"

int main(int argc, char **argv) {
  CHECK(argc == 2);
  std::vector<std::string> lines = Util::ReadFileToLines(argv[1]);

  CryptRand cr;
  ArcFour rc(std::format("santa.{:x}.{:x}", cr.Word64(), cr.Word64()));

  Shuffle(&rc, &lines);

  for (const std::string &line : lines) {
    printf("%s\n", line.c_str());
  }

  return 0;
}
