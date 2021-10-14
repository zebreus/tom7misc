#include "network-gpu.h"

#include <cmath>
#include <memory>

#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"
#include "arcfour.h"
#include "randutil.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

static void ForwardTests(TestNet test_net) {
  printf("\n--------------------------\n"
         "[Forward] Test net: %s\n", test_net.name.c_str());

  Network &net = test_net.net;
  printf("Create NetworkGPU...\n");
  auto net_gpu = make_unique<NetworkGPU>(cl, &net);
  printf("... done creating NetworkGPU.\n");

  for (const TestExample &example : test_net.examples) {
    std::unique_ptr<TrainingRoundGPU> training_round =
      std::make_unique<TrainingRoundGPU>(cl, net);

    training_round->LoadInput(example.input);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net);

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(
          net_gpu.get(), training_round.get(), src_layer);
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


// TODO: Figure out a testing strategy for training stuff;
// right now this just basically tests that it doesn't crash.
static void TrainTests(TestNet test_net) {
  printf("\n--------------------------\n"
         "[Train] Test net: %s\n", test_net.name.c_str());
  ArcFour rc(test_net.name);
  RandomGaussian gauss(&rc);

  Network &net = test_net.net;
  auto net_gpu = make_unique<NetworkGPU>(cl, &net);

  for (const TestExample &example : test_net.examples) {
    std::unique_ptr<TrainingRoundGPU> training_round =
      std::make_unique<TrainingRoundGPU>(cl, net);

    training_round->LoadInput(example.input);

    // Perturb the training example so that it is not exactly what the
    // network already predicts, just so that the errors are not
    // trivial zeroes.
    vector<float> out = example.output;
    for (float &f : out) {
      f += gauss.Next();
    }

    training_round->LoadExpected(out);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net);

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(
          net_gpu.get(), training_round.get(), src_layer);
    }

    std::unique_ptr<SetOutputErrorCL> error_cl =
      std::make_unique<SetOutputErrorCL>(cl, net);

    error_cl->SetOutputError(net_gpu.get(), training_round.get());

    std::unique_ptr<BackwardLayerCL> backward_cl =
      std::make_unique<BackwardLayerCL>(cl, net);

    for (int dst_layer = net.layers.size() - 1;
         // Note we propagate error to the input layer here,
         // which we expect to work, but is pointless during
         // training because there are no weights to update.
         dst_layer > 0;
         dst_layer--) {
      backward_cl->BackwardLayer(net_gpu.get(),
                                 training_round.get(),
                                 dst_layer);
    }

    std::unique_ptr<DecayWeightsCL> decay_cl =
      std::make_unique<DecayWeightsCL>(cl, net, 0.999999f);

    // Note that normally we would not decay weights on the input
    // layer (there are none), but we do it here to test that it does
    // not crash.
    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(net_gpu.get(), layer_idx);
    }

    std::unique_ptr<UpdateWeightsCL> update_cl =
      std::make_unique<UpdateWeightsCL>(cl, net);

    for (int layer_idx = 1; layer_idx < net.layers.size(); layer_idx++) {
      update_cl->Update(net_gpu.get(), training_round.get(),
                        0.001f, layer_idx);
    }
  }

  // Copy back to CPU instance.
  net_gpu->ReadFromGPU();
}


int main(int argc, char **argv) {
  cl = new CL;

  #if 0
  ForwardTests(NetworkTestUtil::SingleSparse());
  ForwardTests(NetworkTestUtil::SingleDense());
  ForwardTests(NetworkTestUtil::SingleConvolution());
  ForwardTests(NetworkTestUtil::TwoInputSparse());
  ForwardTests(NetworkTestUtil::TwoDenseChunks());
  ForwardTests(NetworkTestUtil::Net1());
  #endif

  TrainTests(NetworkTestUtil::SingleSparse());
  TrainTests(NetworkTestUtil::SingleDense());
  TrainTests(NetworkTestUtil::SingleConvolution());
  TrainTests(NetworkTestUtil::TwoInputSparse());
  TrainTests(NetworkTestUtil::TwoDenseChunks());
  TrainTests(NetworkTestUtil::Net1());

  delete cl;

  printf("OK\n");
  return 0;
}
