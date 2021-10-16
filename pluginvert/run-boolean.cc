
#include "network.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>

#include "network-test-util.h"
#include "arcfour.h"
#include "randutil.h"

using namespace std;

// Runs a bool -> bool model with the input vector (1 or true treated
// as 1.0f, others as 0.0f) on the command line.

int main(int argc, char **argv) {
  ArcFour rc("asdf");
  std::vector<int> perm;
  for (int i = 0; i < 256; i++) perm.push_back(i);
  Shuffle(&rc, &perm);
  for (int i : perm) printf("%d, ", i);
  return 0;


  CHECK(argc >= 2) << "./run-boolean.exe model.val b1 b2 b3 ...\n";
  string model = argv[1];

  std::unique_ptr<Network> net(Network::ReadFromFile(model));
  CHECK(net.get() != nullptr);

  Stimulation stim(*net);

  const int INPUT_SIZE = stim.values[0].size();
  // const int OUTPUT_SIZE = stim.values.back().size();
  for (int i = 0; i < INPUT_SIZE && i + 2 < argc; i++) {
    string arg = argv[i + 2];
    stim.values[0][i] = (arg == "true" || arg == "1") ? 1.0f : 0.0f;
  }

  printf("Input:");
  for (float f : stim.values[0]) printf(" %.1f", f);
  printf("\n");

  net->RunForward(&stim);

  for (int layer_idx = 1; layer_idx < stim.values.size(); layer_idx++) {
    printf(" ==== layer %d ====\n", layer_idx);
    for (const float f : stim.values[layer_idx])
      printf("%.2f ", f);
    printf("\n\n");
  }

  printf("Output as booleans:\n");

  // XXX these customizations are just for the referenced network...
  int total_wrong = 0;
  const std::vector<float> reference =
    NetworkTestUtil::LearnBoolean().f(stim.values[0]);
  const std::vector<float> &out = stim.values.back();
  CHECK(out.size() == 256);
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x ++) {
      const int i = y * 16 + x;
      bool ref = reference[i] > 0.5f;
      bool got = out[i] > 0.5f;
      bool wrong = ref != got;
      if (wrong) total_wrong++;
      printf("%c%c%c%c",
             (wrong ? '[' : ' '),
             (ref ? '#' : '.'),
             (got ? '@' : '-'),
             (wrong ? ']' : ' '));
    }
    printf("\n");
  }
  printf("Wrong: %d\n", total_wrong);

  return 0;
}
