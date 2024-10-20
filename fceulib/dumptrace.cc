
#include <cstdio>
#include <vector>
#include <string>

#include "trace.h"

using namespace std;

using Trace = Traces::Trace;

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr,
            "Dumps a trace file in ASCII format to stdout.\n");
    return -1;
  }

  vector<Trace> left = Traces::ReadFromFile(argv[1]);
  fprintf(stderr, "Loaded %lld traces from %s.\n", left.size(), argv[1]);


  for (int i = 0; i < left.size(); i++) {
    printf("%s\n", Traces::LineString(left[i]).c_str());
  }

  return 0;
}
