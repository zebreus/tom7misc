
#include "error-history.h"

// XXX should be configurable on command line
static constexpr int NUM_MODELS = 1;
static constexpr int MAX_POINTS = 1000;

int main(int argc, char **argv) {

  ErrorHistory history("error.tsv", NUM_MODELS);

  history.WriteMergedTSV("merged-error.tsv", {MAX_POINTS});

  return 0;
}
