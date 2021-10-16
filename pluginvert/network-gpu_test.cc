#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>

#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
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
static void TrainOnTestTests(TestNet test_net) {
  printf("\n--------------------------\n"
         "[TrainOnTest] Test net: %s\n", test_net.name.c_str());
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

static void TrainTest(TrainNet train_net,
                      int max_iterations,
                      int examples_per_round,
                      // per round, not example
                      float learning_rate,
                      int max_parallelism) {
  // 0, 1, 2
  static constexpr int VERBOSE = 0;
  const float example_learning_rate = learning_rate / (float)examples_per_round;

  printf("\n--------------------------\n"
         "[Train] Train net: %s\n", train_net.name.c_str());
  ArcFour rc(train_net.name);
  RandomGaussian gauss(&rc);

  Network &net = train_net.net;
  // Initialize with random weights.
  RandomizeNetwork(&rc, &net, 2);

  auto net_gpu = make_unique<NetworkGPU>(cl, &net);

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net);
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net);
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net);
  [[maybe_unused]]
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net, 0.9999f);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, net);

  // Uninitialized training examples on GPU.
  std::vector<std::unique_ptr<TrainingRoundGPU>> training;
  for (int i = 0; i < examples_per_round; i++)
    training.emplace_back(new TrainingRoundGPU(cl, net));

  // Used to compute loss.
  std::vector<std::vector<float>> expected;
  expected.resize(training.size());

  for (int iter = 0; iter < max_iterations; iter++) {

    // Initialize training examples.
    // (PERF: parallelize?)
    for (int i = 0; i < training.size(); i++) {
      std::vector<float> inputs;
      inputs.reserve(train_net.NumInputs());
      for (int j = 0; j < train_net.NumInputs(); j++) {
        if (train_net.boolean_problem) {
          inputs.push_back(rc.Byte() < 128 ? 1.0f : 0.0f);
        } else {
          inputs.push_back(gauss.Next());
        }
      }
      training[i]->LoadInput(inputs);
      std::vector<float> outputs = train_net.f(inputs);
      training[i]->LoadExpected(outputs);
      expected[i] = std::move(outputs);
    }

    if (VERBOSE > 1)
      printf("Prepped examples.\n");

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      ParallelComp(
          training.size(),
          [&](int idx) {
            forward_cl->RunForward(
                net_gpu.get(), training[idx].get(), src_layer);
          },
          max_parallelism);
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    ParallelComp(
        training.size(),
        [&](int idx) {
          error_cl->SetOutputError(net_gpu.get(), training[idx].get());
        },
        max_parallelism);

    if (VERBOSE > 1)
      printf("Set error.\n");

    for (int dst_layer = net.layers.size() - 1;
         // Don't propagate to input.
         dst_layer > 1;
         dst_layer--) {
      ParallelComp(
          training.size(),
          [&](int idx) {
            backward_cl->BackwardLayer(net_gpu.get(),
                                       training[idx].get(),
                                       dst_layer);
          },
          max_parallelism);
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(net_gpu.get(), layer_idx);
    }

    // Can't run training examples in parallel because these all write
    // to the same network. But each later is independent.
    ParallelComp(net.layers.size() - 1,
                 [&](int layer_minus_1) {
                   const int layer_idx = layer_minus_1 + 1;
                   for (int i = 0; i < training.size(); i++) {
                     update_cl->Update(net_gpu.get(), training[i].get(),
                                       example_learning_rate, layer_idx);
                   }
                 },
                 max_parallelism);

    if (VERBOSE > 1)
      printf("Updated errors.\n");

    // Get loss for examples.
    // Size of examples = Number of training instances.
    std::vector<float> losses =
      ParallelMapi(expected,
                   [&](int idx, const std::vector<float> exp) {
                     std::vector<float> got;
                     got.resize(exp.size());
                     training[idx]->ExportOutput(&got);

                     float loss = 0.0f;
                     for (int i = 0; i < exp.size(); i++)
                       loss += fabsf(exp[i] - got[i]);
                     return loss;
                   }, max_parallelism);

    if (VERBOSE > 1)
      printf("Got losses.\n");

    float min_loss = 1.0f / 0.0f, average_loss = 0.0f, max_loss = 0.0f;
    for (float f : losses) {
      min_loss = std::min(f, min_loss);
      max_loss = std::max(f, max_loss);
      average_loss += f;
    }
    average_loss /= losses.size();

    // Parameter for termination?
    if (average_loss < 0.0001f) {
      printf("Successfully trained!\n");
      return;
    } else {
      if (VERBOSE || (iter % 100 == 0))
        printf("%d: %.3f < %.3f < %.3f\n", iter,
               min_loss, average_loss, max_loss);
    }

    if (iter % 5000 == 0) {
      net_gpu->ReadFromGPU();
      const string file = StringPrintf("gpu-test-net-%d.val", iter);
      net.SaveToFile(file);
      printf("Wrote %s\n", file.c_str());
    }
  }

  printf("Didn't train after %d iterations :(\n", max_iterations);

  // Copy back to CPU instance.
  // net_gpu->ReadFromGPU();
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

  TrainOnTestTests(NetworkTestUtil::SingleSparse());
  TrainOnTestTests(NetworkTestUtil::SingleDense());
  TrainOnTestTests(NetworkTestUtil::SingleConvolution());
  TrainOnTestTests(NetworkTestUtil::TwoInputSparse());
  TrainOnTestTests(NetworkTestUtil::TwoDenseChunks());
  TrainOnTestTests(NetworkTestUtil::Net1());
  #endif

  #if 0
  TrainTest(NetworkTestUtil::LearnTrivialIdentitySparse(),
            1000, 1000, 1.0f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityDense(),
            1000, 1000, 1.0f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityConvolution(),
            1000, 1000, 1.0f, 4);
  #endif
  // Smaller batch size since there are only 2^8 possible inputs.
  TrainTest(NetworkTestUtil::LearnBoolean(),
            100000, 64, 0.1f, 4);
  // 16 dense, 256 dense
  // After 13400 rounds: 13400: 4.809 < 19.069 < 33.299
  // 16 dense + 3 id, 256 dense
  // After 10000 rounds: 10000: 8.101 < 12.146 < 14.820

  delete cl;

  printf("OK\n");
  return 0;
}
