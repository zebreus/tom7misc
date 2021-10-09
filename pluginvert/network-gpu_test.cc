#include "network-gpu.h"

#include <cmath>
#include <memory>

#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TestExample = NetworkTestUtil::TestExample;

static void ForwardTests(TestNet test_net) {
  printf("\n--------------------------\n"
         "Test net: %s\n", test_net.name.c_str());
  // Or global??
  std::unique_ptr<CL> cl = std::make_unique<CL>();

  Network &net = test_net.net;
  printf("Create NetworkGPU...\n");
  auto net_gpu = make_unique<NetworkGPU>(cl.get(), &net);
  printf("... done creating NetworkGPU.\n");

  for (const TestExample &example : test_net.examples) {
    std::unique_ptr<TrainingRoundGPU> training_round =
      std::make_unique<TrainingRoundGPU>(cl.get(), net);

    training_round->LoadInput(example.input);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl.get(), net);

    {
      // XXX don't keep this, because it reads uninitialized mem
      Stimulation stim(net);
      training_round->ExportStimulation(&stim);
      // No change to input layer.
      CHECK_FEQV(stim.values[0], example.input);
    }

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      printf("Forward src_layer %d ...\n", src_layer);
      forward_cl->RunForward(
          net_gpu.get(), training_round.get(), src_layer);

      {
        // XXX don't keep this, because it reads uninitialized mem
        Stimulation stim(net);
        training_round->ExportStimulation(&stim);
        // No change to input layer.
        CHECK_FEQV(stim.values[0], example.input);
      }

    }

    // Must be initialized to the correct size.
    std::vector<float> gpu_out(net.layers.back().num_nodes, -1.0f);
    training_round->ExportOutput(&gpu_out);

    CHECK_FEQV(gpu_out, example.output);

    // And via the stimulation, which should be the same...
    Stimulation stim(net);
    training_round->ExportStimulation(&stim);
    // No change to input layer.
    CHECK_FEQV(stim.values[0], example.input);
    // Should have expected output after inference.
    CHECK_FEQV(stim.values.back(), example.output);
  }
}


int main(int argc, char **argv) {
  ForwardTests(NetworkTestUtil::SingleSparse());
  ForwardTests(NetworkTestUtil::SingleDense());
  ForwardTests(NetworkTestUtil::TwoInputSparse());
  ForwardTests(NetworkTestUtil::TwoDenseChunks());
  ForwardTests(NetworkTestUtil::Net1());

  printf("OK\n");
  return 0;
}
