#include <string>
#include <vector>
#include <cstddef>

#include "inclusion.h"
#include "util.h"
#include "base/logging.h"

int main(int argc, char **argv) {

  std::vector<std::string> includepaths;

  std::vector<std::string> leftover;
  for (int i = 1; i < (int)argc; i++) {
    std::string_view arg(argv[i]);
    if (Util::TryStripPrefix("-I", &arg)) {
      // Then this is -Istdlib
      if (!arg.empty()) {
        includepaths.push_back((std::string)arg);
      } else {
        CHECK(i + 1 < argc) << "Trailing -I argument";
        i++;
        includepaths.push_back(std::string(argv[i]));
      }
    } else {
      leftover.push_back((std::string)arg);
    }
  }

  CHECK(leftover.size() == 1) <<
    "./concat [-I path1 -I path2 ...] source.bovex\n"
    "Give a single source file. It's loaded with its includes like\n"
    "bovex would do. Prints the concatenated source file on\n"
    "stdout.\n\n";

  const std::string &filename = leftover[0];

  const auto &[source, tokens, source_map] =
    Inclusion::Process(includepaths, filename);

  printf("%s\n", source.c_str());
}
