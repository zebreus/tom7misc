
// Randomize the lines in a file, very securely!

#include "util.h"
#include "arcfour.h"

#include <vector>
#include <string>

#include "crypt/cryptrand.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

int main(int argc, char **argv) {
  CHECK(argc == 2);
  std::vector<std::string> lines = Util::ReadFileToLines(argv[1]);

  CryptRand cr;
  ArcFour rc(StringPrintf("santa.%llx.%llx", cr.Word64(), cr.Word64()));

  Shuffle(&rc, &lines);

  for (const std::string &line : lines) {
    printf("%s\n", line.c_str());
  }

  return 0;
}
