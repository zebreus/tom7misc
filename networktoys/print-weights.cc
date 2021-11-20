
#include "modelinfo.h"

#include <memory>
#include <string>
#include <cstdint>
#include <cmath>
#include <time.h>

#include "network.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

int main(int argc, char **argv) {
  CHECK(argc == 4) << "Usage:\n"
    "print-weights.exe net0.val layer_idx chunk_idx";
  const string modelfile = argv[1];
  const int layer_idx = atoi(argv[2]);
  const int chunk_idx = atoi(argv[3]);

  // Try loading from disk; null on failure.
  printf("Load network from %s...\n", modelfile.c_str());
  std::unique_ptr<Network> net(Network::ReadFromFile(modelfile));
  CHECK(net.get() != nullptr) << modelfile;

  CHECK(layer_idx >= 0 && layer_idx < net->layers.size()) << layer_idx;
  const Layer &layer = net->layers[layer_idx];
  CHECK(chunk_idx >= 0 && chunk_idx < layer.chunks.size()) << chunk_idx;
  const Chunk &chunk = layer.chunks[chunk_idx];

  printf("Weights for %s %d.%d:\n", modelfile.c_str(), layer_idx, chunk_idx);
  for (float f : chunk.weights) {
    printf("  %.6f\n", f);
  }
  printf("Biases for %s %d.%d:\n", modelfile.c_str(), layer_idx, chunk_idx);
  for (float f : chunk.biases) {
    printf("  %.6f\n", f);
  }

  return 0;
}
