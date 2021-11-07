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
      std::make_unique<TrainingRoundGPU>(1, cl, net);

    training_round->LoadInput(0, example.input);

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
    training_round->ExportOutput(0, &gpu_out);

    CHECK_FEQV(gpu_out, example.output);

    // And via the stimulation, which should be the same...
    Stimulation stim(net);
    training_round->ExportStimulation(0, &stim);
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
      std::make_unique<TrainingRoundGPU>(1, cl, net);

    training_round->LoadInput(0, example.input);

    // Perturb the training example so that it is not exactly what the
    // network already predicts, just so that the errors are not
    // trivial zeroes.
    vector<float> out = example.output;
    for (float &f : out) {
      f += gauss.Next();
    }

    training_round->LoadExpected(0, out);

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
      std::make_unique<UpdateWeightsCL>(training_round->num_examples,
                                        cl, net);

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
                      // per round (e.g. 0.01f), not example
                      float learning_rate,
                      float avg_loss_threshold,
                      int max_parallelism) {
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;

  std::vector<std::unique_ptr<ImageRGBA>> images;
  constexpr int IMAGE_WIDTH = 3000;
  constexpr int IMAGE_HEIGHT = 1000;
  constexpr int IMAGE_EVERY = 5;
  int image_x = 0;
  for (int i = 0; i < train_net.net.layers.size(); i++) {
    images.emplace_back(new ImageRGBA(IMAGE_WIDTH, IMAGE_HEIGHT));
    images.back()->Clear32(0x000000FF);
    images.back()->BlendText2x32(
        2, 2, 0x9999AAFF,
        StringPrintf("Train test: %s | Layer %d | 1px = %d rounds ",
                     train_net.name.c_str(), i,
                     train_net.net.rounds, IMAGE_EVERY));
  }

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
    std::make_unique<UpdateWeightsCL>(examples_per_round, cl, net);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(examples_per_round, cl, net));

  // Used to compute loss.
  std::vector<std::vector<float>> expected;
  expected.resize(training->num_examples);


  Timer train_timer;
  int64 total_examples = 0LL;
  for (int iter = 0; iter < max_iterations; iter++) {
    std::vector<float> flat_inputs, flat_outputs;
    flat_inputs.reserve(train_net.NumInputs() * examples_per_round);
    flat_outputs.reserve(train_net.NumOutputs() * examples_per_round);

    // Initialize training examples.
    // (PERF: insert these all in a batch; it's much faster)
    for (int i = 0; i < training->num_examples; i++) {
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

      // Much faster to load these in a batch.
      for (float f : inputs) flat_inputs.push_back(f);
      std::vector<float> outputs = train_net.f(inputs);
      CHECK(outputs.size() == train_net.NumOutputs());
      for (float f : outputs) flat_outputs.push_back(f);
      // PERF could save the flat outputs and base on that
      expected[i] = std::move(outputs);
    }

    training->LoadInputs(flat_inputs);
    training->LoadExpecteds(flat_outputs);

    if (VERBOSE > 1)
      printf("Prepped examples.\n");

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(net_gpu.get(), training.get(), src_layer);
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    error_cl->SetOutputError(net_gpu.get(), training.get());

    if (VERBOSE > 1)
      printf("Set error.\n");

    for (int dst_layer = net.layers.size() - 1;
         // Don't propagate to input.
         dst_layer > 1;
         dst_layer--) {
      backward_cl->BackwardLayer(net_gpu.get(), training.get(), dst_layer);
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(net_gpu.get(), layer_idx);
    }

    // PERF: No benefit to parallelism here currently, as each takes
    // the global mutex. A future version might, though.
    ParallelComp(net.layers.size() - 1,
                 [&](int layer_minus_1) {
                   const int layer_idx = layer_minus_1 + 1;
                   update_cl->Update(net_gpu.get(), training.get(),
                                     learning_rate, layer_idx);
                 },
                 max_parallelism);

    if (VERBOSE > 1)
      printf("Updated errors.\n");

    // PERF: Consider only doing this every few rounds, as it is probably
    // the bottleneck in these tests now.

    // Get loss as abs distance, plus number of incorrect (as booleans).
    // Size of examples = Number of training instances.
    std::vector<std::pair<float, int>> losses =
      ParallelMapi(expected,
                   [&](int idx, const std::vector<float> exp) {
                     std::vector<float> got;
                     got.resize(exp.size());
                     training->ExportOutput(idx, &got);

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

    net.examples += examples_per_round;
    net.rounds++;

    const bool finished =
      train_net.boolean_output ?
      max_inc == 0 : average_loss < avg_loss_threshold;

    if (finished || (iter % IMAGE_EVERY) == 0) {

      // XXX would be better if this was more accurate,
      // but we only want to read from GPU if we're going to
      // actually do anything below
      if (images.size() >= 2 &&
          images[1].get() != nullptr &&
          image_x < images[1]->Width()) {
        net_gpu->ReadFromGPU();

        for (int target_layer = 1; target_layer < net.layers.size();
             target_layer++) {
          const Layer &layer = net.layers[target_layer];
          ImageRGBA *image = images[target_layer].get();
          // Note we only graph the first chunk of each layer...
          CHECK(layer.chunks.size() > 0);
          const Chunk &chunk = layer.chunks[0];
          auto ToScreenY = [](float w) {
              int yrev = w * float(IMAGE_HEIGHT / 4) + (IMAGE_HEIGHT / 2);
              int y = IMAGE_HEIGHT - yrev;
              // Always draw on-screen.
              return std::clamp(y, 0, IMAGE_HEIGHT - 1);
            };
          // 1, -1, x axis
          if (image_x & 1) {
            image->BlendPixel32(image_x, ToScreenY(+1), 0xCCFFCC40);
            image->BlendPixel32(image_x, ToScreenY( 0), 0xCCCCFFFF);
            image->BlendPixel32(image_x, ToScreenY(-1), 0xFFCCCC40);
          }

          const uint8 weight_alpha =
            std::clamp((255.0f / sqrtf(chunk.weights.size())), 10.0f, 240.0f);

          for (float w : chunk.weights) {
            // maybe better to AA this?
            image->BlendPixel32(image_x, ToScreenY(w),
                                0xFFFFFF00 | weight_alpha);
          }

          const uint8 bias_alpha =
            std::clamp((255.0f / sqrtf(chunk.biases.size())), 10.0f, 240.0f);

          for (float b : chunk.biases) {
            image->BlendPixel32(image_x, ToScreenY(b),
                                0xFF777700 | bias_alpha);
          }

          if (chunk.weight_update == ADAM) {
            CHECK(chunk.weights_aux.size() == 2 * chunk.weights.size());
            CHECK(chunk.biases_aux.size() == 2 * chunk.biases.size());
            for (int idx = 0; idx < chunk.weights.size(); idx++) {
              const float m = chunk.weights_aux[idx * 2 + 0];
              const float v = sqrtf(chunk.weights_aux[idx * 2 + 1]);

              image->BlendPixel32(image_x, ToScreenY(m),
                                  0xFFFF0000 | weight_alpha);
              image->BlendPixel32(image_x, ToScreenY(v),
                                  0xFF00FF00 | weight_alpha);
            }

            // Too many dots??
            for (int idx = 0; idx < chunk.biases.size(); idx++) {
              const float m = chunk.biases_aux[idx * 2 + 0];
              const float v = sqrtf(chunk.biases_aux[idx * 2 + 1]);

              image->BlendPixel32(image_x, ToScreenY(m),
                                  0xFF770000 | bias_alpha);
              image->BlendPixel32(image_x, ToScreenY(v),
                                  0xFF007700 | bias_alpha);
            }
          }

          if ((image_x % 100 == 0) || image_x == image->Width()) {
            string filename = StringPrintf("train-test-image-%d.png",
                                           target_layer);
            image->Save(filename);
            printf("Wrote %s\n", filename.c_str());
          }
        }
        image_x++;
      }
    }

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

  printf("Failed on %s:\n"
         "Didn't converge after %d iterations :(\n",
         train_net.name.c_str(),
         max_iterations);

  LOG(FATAL) << "Failed";

  // Copy back to CPU instance.
  // net_gpu->ReadFromGPU();
}

int main(int argc, char **argv) {
  cl = new CL;

  #if 1
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

  #if 1
  TrainTest(NetworkTestUtil::LearnTrivialIdentitySparse(),
            1000, 1000, 1.0f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityDense(),
            1000, 1000, 1.0f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::LearnTrivialIdentityConvolution(),
            1000, 1000, 1.0f, 0.0001f, 4);
  #endif

  #if 0
  // Smaller batch size since there are only 2^3 possible inputs.
  // Should achieve zero boolean errors after about 1000 rounds;
  // absolute error continues falling.
  // With a less aggressive learning rate, this can take many
  // thousands of rounds to converge (or never converge).
  TrainTest(NetworkTestUtil::LearnBoolean(),
            6000, 54, 0.1f, 0.0001f, 4);
  #endif

  #if 0
  // XXX these get to like 0.000 but not the same convergence
  // we had before "fixing" adam .... probably ok??

  // ADAM tests. These each take about 400 rounds to converge,
  // because here we are using a sensible learning rate of 0.01f.
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnTrivialIdentitySparse()),
            1000, 1000, 0.01f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnTrivialIdentityDense()),
            1000, 1000, 0.01f, 0.0001f, 4);
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnTrivialIdentityConvolution()),
            1000, 1000, 0.01f, 0.0001f, 4);
  #endif


  #if 1
  // Even with a lower learning rate, this converges much faster than
  // the SGD version :) ~200 rounds.
  TrainTest(NetworkTestUtil::ForceAdam(NetworkTestUtil::LearnBoolean()),
            6000, 54, 0.01f, 0.0001f, 4);
  #endif


  #if 0
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
  #endif

  #if 0
  // Adam works well on this, even with a conservative learning
  // rate of 0.01f; once the weights get near 1, the bias rapidly
  // gets unlearned. Converges in <4000 4ounds.
  // (XXX this is now like 3000 rounds with fixed adam)
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnCountOnesDense()),
            10000, 1000, 0.01f,
            0.100f,
            4);
  #endif


  #if 0
  // Counting can be done with a convolution; this stacks a 4->1
  // convolution and a 5->1 convolution, followed by a dense layer
  // for the rest. (The dense layer is currently fixed!)
  TrainTest(NetworkTestUtil::LearnCountOnesConvConvDense(),
            10000, 1000, 0.001f,
            0.100f,
            4);
  #endif

  #if 1
  // With fixed adam this converges in <4000 rounds.
  // TODO: Try removing the fixed constraint on the dense layer.
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnCountOnesConvConvDense()),
            10000, 1000, 0.01f,
            0.01f,
            4);
  #endif


  #if 0
  // With fixed, adam, converges in about 5100 rounds.
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnCountOnesConvDense()),
            10000, 1000, 0.01f,
            0.010f,
            4);
  #endif

  #if 0
  // Does converge in ~17000 rounds. Seems to be dependent on initial
  // conditions (as there is a late "breakthrough"), and perhaps with
  // more dice rolls for the features it would be pretty fast.
  TrainTest(NetworkTestUtil::ForceAdam(
                NetworkTestUtil::LearnCountEdges()),
            20000, 1000, 0.01f,
            0.010f,
            4);
  #endif

  delete cl;

  printf("OK\n");
  return 0;
}
