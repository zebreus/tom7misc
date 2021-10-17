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
#include "image.h"

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
                      float avg_loss_threshold,
                      int max_parallelism) {
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;
  const float example_learning_rate = learning_rate / (float)examples_per_round;

  // XXX!
  std::unique_ptr<ImageRGBA> image;
  constexpr int IMAGE_WIDTH = 2000;
  constexpr int IMAGE_HEIGHT = 1000;
  constexpr int IMAGE_EVERY = 1;
  int image_x = 0;
  image.reset(new ImageRGBA(IMAGE_WIDTH, IMAGE_HEIGHT));
  image->Clear32(0x000000FF);

  printf("\n--------------------------\n"
         "[Train] Train net: %s\n", train_net.name.c_str());
  ArcFour rc(train_net.name + "XXX");
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
    std::make_unique<DecayWeightsCL>(cl, net, 0.99999f);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, net);

  // Uninitialized training examples on GPU.
  std::vector<std::unique_ptr<TrainingRoundGPU>> training;
  for (int i = 0; i < examples_per_round; i++)
    training.emplace_back(new TrainingRoundGPU(cl, net));

  // Used to compute loss.
  std::vector<std::vector<float>> expected;
  expected.resize(training.size());

  Timer train_timer;
  int64 total_examples = 0LL;
  for (int iter = 0; iter < max_iterations; iter++) {

    // Initialize training examples.
    // (PERF: parallelize?)
    for (int i = 0; i < training.size(); i++) {
      std::vector<float> inputs;
      inputs.reserve(train_net.NumInputs());

      if (train_net.boolean_input) {
        // For boolean problems, a combination of random
        // bit-strings and sparse ones.
        switch (rc.Byte() & 1) {
        default:
        case 0:
          for (int j = 0; j < train_net.NumInputs(); j++) {
            inputs.push_back(rc.Byte() < 128 ? 1.0f : 0.0f);
          }
          break;
        case 1: {
          // Choose a threshold, which yields strings biased
          // towards 0 or 1.
          const uint8 threshold = rc.Byte();
          for (int j = 0; j < train_net.NumInputs(); j++) {
            inputs.push_back(rc.Byte() < threshold ? 1.0f : 0.0f);
          }
          break;
        }
        }
      } else {
        // Could perhaps consider other distributions? Or the
        // problem could specify it?
        for (int j = 0; j < train_net.NumInputs(); j++) {
          inputs.push_back(gauss.Next());
        }
      }
      training[i]->LoadInput(inputs);
      std::vector<float> outputs = train_net.f(inputs);
      CHECK(outputs.size() == train_net.NumOutputs());
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
    // to the same network. But each layer is independent.
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

    // Get loss as abs distance, plus number of incorrect (as booleans).
    // Size of examples = Number of training instances.
    std::vector<std::pair<float, int>> losses =
      ParallelMapi(expected,
                   [&](int idx, const std::vector<float> exp) {
                     std::vector<float> got;
                     got.resize(exp.size());
                     training[idx]->ExportOutput(&got);

                     int incorrect = 0;
                     float loss = 0.0f;
                     for (int i = 0; i < exp.size(); i++) {
                       loss += fabsf(exp[i] - got[i]);
                       // for boolean problems only, but we compute
                       // it either way
                       bool want = exp[i] > 0.5f;
                       bool made = got[i] > 0.5f;
                       if (want != made) incorrect++;
                     }
                     return std::make_pair(loss, incorrect);
                   }, max_parallelism);

    if (VERBOSE > 1)
      printf("Got losses.\n");

    float min_loss = 1.0f / 0.0f, average_loss = 0.0f, max_loss = 0.0f;
    int min_inc = net.layers.back().num_nodes + 1, max_inc = 0;
    float average_inc = 0.0f;
    for (auto [loss_dist, loss_incorrect] : losses) {
      min_loss = std::min(loss_dist, min_loss);
      max_loss = std::max(loss_dist, max_loss);
      average_loss += loss_dist;

      min_inc = std::min(loss_incorrect, min_inc);
      max_inc = std::max(loss_incorrect, max_inc);
      average_inc += loss_incorrect;
    }
    average_loss /= losses.size();
    average_inc /= losses.size();

    total_examples += examples_per_round;
    const double total_sec = train_timer.MS() / 1000.0;
    const double eps = total_examples / total_sec;

    // XXX
    if (image.get() != nullptr && (iter % IMAGE_EVERY == 0) &&
        image_x < image->Width()) {
      net_gpu->ReadFromGPU();
      CHECK(net.layers.size() > 0);
      CHECK(net.layers[1].chunks.size() == 1);
      // x axis
      auto ToScreenY = [](float w) {
          int yrev = w * float(IMAGE_HEIGHT / 4) + (IMAGE_HEIGHT / 2);
          int y = IMAGE_HEIGHT - yrev;
          return y;
        };
      image->BlendPixel32(image_x, ToScreenY(0), 0xCCCCFFFF);
      for (float w : net.layers[1].chunks[0].weights) {
        // maybe better to AA this?
        image->BlendPixel32(image_x, ToScreenY(w), 0xFFFFFF20);
      }

      CHECK(net.layers[1].chunks[0].biases.size() == 1);
      float b = net.layers[1].chunks[0].biases[0];
      image->BlendPixel32(image_x, ToScreenY(b), 0xFF7777A0);

      image_x++;
      if ((image_x % 100 == 0) || image_x == image->Width()) {
        image->Save("train-image.png");
        printf("Wrote train-image.png\n");
      }
    }

    const bool finished =
      train_net.boolean_output ? max_inc == 0 : average_loss < avg_loss_threshold;

    if (SAVE_INTERMEDIATE && (finished || iter == 1000 || iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      const string file = StringPrintf("gpu-test-net-%d.val", iter);
      net.SaveToFile(file);
      if (VERBOSE)
        printf("Wrote %s\n", file.c_str());
    }

    // Parameter for average_loss termination?
    if (finished) {
      printf("Successfully trained!\n");
      return;
    } else {
      if (VERBOSE || (iter % 100 == 0)) {
        printf("%d: %.3f < %.3f < %.3f", iter,
               min_loss, average_loss, max_loss);
        if (train_net.boolean_output) {
          printf("  |  %d < %.3f < %d",
                 min_inc, average_inc, max_inc);
        }
        printf(" (%.2f eps)\n", eps);
      }
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
            1000, 1000, 1.0f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityDense(),
            1000, 1000, 1.0f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityConvolution(),
            1000, 1000, 1.0f, 0.0001f, 4);
  // Smaller batch size since there are only 2^8 possible inputs.
  // This should converge to zero (boolean) errors after about
  // 1000 rounds; the absolute error drops to like ~4 after many
  // thousand rounds.
  TrainTest(NetworkTestUtil::LearnBoolean(),
            2000, 54, 0.01f, 0.0001f, 4);
  #endif

  // Interesting example.
  // This has a very simple solution (bias=0, all weights=1), but
  // the average case (bias = input size / 2) is quite far from it;
  // we spend most of the time trying to unlearn that initial
  // bias. I wonder if this is a case where "pre-training" might
  // be useful.
  TrainTest(NetworkTestUtil::LearnCountOnesDense(),
            10000, 1000, 0.02f,
            // Looks like it will eventually drop arbitrarily
            // low if you wait for it. Gets to this threshold
            // before about 600 rounds.
            0.100f,
            4);

  #if 0
  // Doesn't quite work yet
  TrainTest(NetworkTestUtil::LearnCountEdges(),
            10000, 1000, 0.01f,
            0.100f,
            4);
  #endif

  delete cl;

  printf("OK\n");
  return 0;
}
