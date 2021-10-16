
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
  CHECK(argc == 3) << "Usage:\nget-layerweights.exe net0.val outputbasename";
  const string modelfile = argv[1];
  const string outputbase = argv[2];

  // Try loading from disk; null on failure.
  printf("Load network from %s...\n", modelfile.c_str());
  std::unique_ptr<Network> net(Network::ReadFromFile(modelfile));

  CHECK(net.get() != nullptr) << modelfile;

  for (int i = 1; i < net->layers.size(); i++) {
    const Layer &layer = net->layers[i];
    for (int c = 0; c < layer.chunks.size(); c++) {
      const ImageRGBA img = ModelInfo::ChunkWeights(*net, i, c, false);

      const string outfile = StringPrintf("%s-layer%d.%d.png",
                                          outputbase.c_str(), i, c);
      img.Save(outfile);
      printf("Wrote %s.\n", outfile.c_str());
    }
  }

  return 0;
}
