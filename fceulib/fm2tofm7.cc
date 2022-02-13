/* This standalone program is part of the fceulib distribution, but shouldn't
   be compiled into it. */

#include <vector>
#include <string>

#include "simplefm2.h"
#include "simplefm7.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "util.h"

static void Usage() {
  fprintf(stderr,
          "Convert an fm2 file to fm7 format."
          "Usage: fm2tofm7 [-cc] karate.fm2 [karate.fm7]\n\n"
          "If -cc is given (must be first) then output as C++ string\n"
          "literal. If no output file is given, writes to stdout.\n");
}  

int main(int argc, char **argv) {
  if (argc < 2) {
    Usage();
    return -1;
  }

  int idx = 1;
  bool cc = false;
  if (0 == strcmp(argv[1], "-cc")) {
    cc = true;
    idx++;
  }

  string infile = argv[idx++];
  string outfile;
  if (idx < argc) {
    outfile = argv[idx++];
  }

  CHECK(infile != "-cc");
  CHECK(outfile != "-cc");
  
  vector<pair<uint8, uint8>> inputs = SimpleFM2::ReadInputs2P(infile);
  fprintf(stderr, "Loaded %s with %lld inputs.\n",
          infile.c_str(), inputs.size());

  const string fm7 = cc ? SimpleFM7::EncodeInputsLiteral2P(inputs, 6, 68) :
    SimpleFM7::EncodeInputs2P(inputs);

  if (outfile.empty()) {
    printf("%s\n", fm7.c_str());
  } else {
    Util::WriteFile(outfile, fm7);
  }

  return 0;
}
