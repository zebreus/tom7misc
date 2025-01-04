
#include "network.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>
#include <optional>

#include "network-test-util.h"
#include "util.h"

using namespace std;

// Runs a network from a vector of inputs to a vector of outputs.

int main(int argc, char **argv) {
  CHECK(argc >= 2) << "./run-network.exe model.val v1 v3 v3 ...\n";
  string model = argv[1];

  // PERF use GPU version if network is large enough
  std::unique_ptr<Network> net(Network::ReadFromFile(model));
  CHECK(net.get() != nullptr);

  Stimulation stim(*net);

  const int INPUT_SIZE = stim.values[0].size();
  CHECK(INPUT_SIZE == argc - 2) << model << " wants "
                                << INPUT_SIZE << " inputs";
  // const int OUTPUT_SIZE = stim.values.back().size();
  for (int i = 0; i < INPUT_SIZE && i + 2 < argc; i++) {
    string arg = argv[i + 2];
    optional<double> doubleo = Util::ParseDoubleOpt(arg);
    CHECK(doubleo.has_value()) << "Couldn't parse number: " << arg;
    stim.values[0][i] = doubleo.value();
  }

  printf("Input:");
  for (float f : stim.values[0]) printf(" %.1f", f);
  printf("\n");

  net->RunForward(&stim);

  // Make hidden layers optional...
  for (int layer_idx = 1; layer_idx < stim.values.size(); layer_idx++) {
    printf(" ==== layer %d ====\n", layer_idx);
    for (const float f : stim.values[layer_idx])
      printf("%.2f ", f);
    printf("\n\n");
  }

  return 0;
}
