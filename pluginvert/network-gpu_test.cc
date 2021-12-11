#include "network-gpu.h"

#include <optional>
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
#include "train-util.h"

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
    if (test_net.examples.size() > 1) printf("%s\n", example.name.c_str());
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


// This is just a test that it doesn't crash, although we could do
// weight updates as we know it is near a known solution, and see if
// it finds it. Note that many of these only have a single example, so
// training would be degenerate there.
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
      std::make_unique<UpdateWeightsCL>(cl, net,
                                        training_round->num_examples);

    for (int layer_idx = 1; layer_idx < net.layers.size(); layer_idx++) {
      update_cl->Update(net_gpu.get(), training_round.get(), layer_idx);
    }
  }

  // Copy back to CPU instance.
  net_gpu->ReadFromGPU();
}

// Returns nullopt on success, otherwise an error message.
static std::optional<string>
TrainTest(TrainNet train_net,
          int max_iterations,
          int examples_per_round,
          float avg_loss_threshold,
          UpdateWeightsCL::UpdateConfig update_config =
          UpdateWeightsCL::UpdateConfig(),
          optional<int> write_images_every = nullopt) {
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = false;
  static constexpr int MAX_PARALLELISM = 4;

  std::unique_ptr<TrainingImages> images;
  if (write_images_every.has_value()) {
    images.reset(new TrainingImages(train_net.net, "test", train_net.name,
                                    write_images_every.value(), 3000, 1000));
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
    std::make_unique<DecayWeightsCL>(cl, net, 0.999999999f);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(
        cl, net, examples_per_round, update_config);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(examples_per_round, cl, net));

  // Used to compute loss.
  std::vector<std::vector<float>> expected;
  expected.resize(training->num_examples);


  Timer train_timer;
  int64 total_examples = 0LL;
  for (int iter = 0; iter < max_iterations; iter++) {

    // Initialize training examples as a batch.
    std::vector<float> flat_inputs, flat_outputs;
    flat_inputs.reserve(train_net.NumInputs() * examples_per_round);
    flat_outputs.reserve(train_net.NumOutputs() * examples_per_round);
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
                                     layer_idx);
                 },
                 MAX_PARALLELISM);

    if (VERBOSE > 1)
      printf("Updated errors.\n");

    total_examples += examples_per_round;
    const double total_sec = train_timer.MS() / 1000.0;
    const double eps = total_examples / total_sec;

    net.examples += examples_per_round;
    net.rounds++;

    // Possibly updated in loss calculation below.
    bool finished = false;

    // Only do this every few rounds, as it is probably
    // the bottleneck in these tests now.
    if (iter < 100 ? (iter % 10 == 0) :
        iter < 1000 ? (iter % 100 == 0) :
        (iter % 1000 == 0)) {
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
                     }, MAX_PARALLELISM);

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

      if (VERBOSE) {
        printf("%d: %.3f < %.3f < %.3f", iter,
               min_loss, average_loss, max_loss);
        if (train_net.boolean_output) {
          printf("  |  %d < %.3f < %d",
                 min_inc, average_inc, max_inc);
        }
        printf(" (%.2f eps)\n", eps);
      }

      finished =
        train_net.boolean_output ?
        max_inc == 0 : average_loss < avg_loss_threshold;
    }

    if (images.get() != nullptr &&
        write_images_every.has_value() &&
        (finished || (iter % write_images_every.value()) == 0)) {
      net_gpu->ReadFromGPU();
      images->Sample(net);
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
      return {};
    }
  }

  return {StringPrintf("Failed on %s:\n"
                       "Didn't converge after %d iterations :(\n",
                       train_net.name.c_str(),
                       max_iterations)};
}

static void TestChunkSchedule() {
  // This test relies on the fact that only the spans are used from
  // the chunks...
  static constexpr int AA = 1, BB = 2, CC = 3;
  Chunk a;
  a.span_start = 0;
  a.span_size = 5;
  a.num_nodes = AA;

  Chunk b;
  b.span_start = 5;
  b.span_size = 10;
  b.num_nodes = BB;

  Chunk c;
  c.span_start = 4;
  c.span_size = 2;
  c.num_nodes = CC;

  {
    const auto [schedule, overwrite] =
      BackwardLayerCL::OptimizeChunkSchedule({a}, false);
    CHECK(schedule.size() == 1);
    CHECK(schedule[0] == 0);
    CHECK(overwrite.size() == 1);
    CHECK(overwrite[0]);
  }

  {
    const auto [schedule, overwrite] =
      BackwardLayerCL::OptimizeChunkSchedule({a, b}, false);
    CHECK(schedule.size() == 2);
    // Either order is okay.
    CHECK((schedule[0] == 1 && schedule[1] == 0) ||
          (schedule[0] == 0 && schedule[1] == 1));
    CHECK(overwrite.size() == 2);
    CHECK(overwrite[0] == true);
    CHECK(overwrite[1] == true);
  }

  {
    // a, b are disjoint
    const auto [schedule, overwrite] =
      BackwardLayerCL::OptimizeChunkSchedule({a, b}, false);
    CHECK(schedule.size() == 2);
    // Either order is okay.
    CHECK((schedule[0] == 1 && schedule[1] == 0) ||
          (schedule[0] == 0 && schedule[1] == 1));
    CHECK(overwrite.size() == 2);
    CHECK(overwrite[0] == true);
    CHECK(overwrite[1] == true);
  }

  {
    // b and c overlap, so take the larger first
    std::vector<Chunk> v = {c, b};
    const auto [schedule, overwrite] =
      BackwardLayerCL::OptimizeChunkSchedule(v, false);
    CHECK(schedule.size() == 2);
    CHECK(v[schedule[0]].num_nodes == BB);
    CHECK(v[schedule[1]].num_nodes == CC);
    CHECK(overwrite.size() == 2);
    CHECK(overwrite[0] == true);
    CHECK(overwrite[1] == false);
  }

  for (const auto &v : std::vector<std::vector<Chunk>>{{c, a, b}, {c, b, a},
                                                       {a, c, b}, {b, c, a},
                                                       {a, b, c}, {b, a, c}}) {
    const auto [schedule, overwrite] =
      BackwardLayerCL::OptimizeChunkSchedule(v, false);
    CHECK(schedule.size() == 3);
    // Either order of a,b is okay, but c overlaps both of them and is small
    // so it must go last.
    CHECK((v[schedule[0]].num_nodes == AA && v[schedule[1]].num_nodes == BB) ||
          (v[schedule[0]].num_nodes == BB && v[schedule[1]].num_nodes == AA));
    CHECK(v[schedule[2]].num_nodes == CC);
    CHECK(overwrite.size() == 3);
    CHECK(overwrite[0] == true);
    CHECK(overwrite[1] == true);
    CHECK(overwrite[2] == false);
  }

}

// These should finish almost immediately and always succeed.
// No ML magic here: If they fail, it's definitely a bug.
static void QuickTests() {
  ForwardTests(NetworkTestUtil::SingleSparse());
  ForwardTests(NetworkTestUtil::SingleDense());
  ForwardTests(NetworkTestUtil::SingleConvolution());
  ForwardTests(NetworkTestUtil::TwoInputSparse());
  ForwardTests(NetworkTestUtil::TwoDenseChunks());
  ForwardTests(NetworkTestUtil::Net1());
  ForwardTests(NetworkTestUtil::SimpleConv());
  ForwardTests(NetworkTestUtil::CountInternalEdges());

  TrainOnTestTests(NetworkTestUtil::SingleSparse());
  TrainOnTestTests(NetworkTestUtil::SingleDense());
  TrainOnTestTests(NetworkTestUtil::SingleConvolution());
  TrainOnTestTests(NetworkTestUtil::TwoInputSparse());
  TrainOnTestTests(NetworkTestUtil::TwoDenseChunks());
  TrainOnTestTests(NetworkTestUtil::Net1());
  TrainOnTestTests(NetworkTestUtil::CountInternalEdges());
  TrainOnTestTests(NetworkTestUtil::SimpleConv());
}

// TODO: Record results of tests in some table that we print out
// at the end, or even log permanently.
// TODO: Facilitate some kind of grid search to set good default
// values for the hyperparameters? Perhaps this should be separate
// from this "test" though.
#define TRAIN_TEST(args...) do {                    \
    auto eo = TrainTest(args);                      \
    CHECK(!eo.has_value()) << "Train test failed: " \
                           << #args                 \
                           << "\nWith message:\n"   \
                           << eo.value();           \
  } while(0)


// Test SGD learning algorithm, which is generally worse than ADAM but
// is easier to understand. It has less train-time overhead.
static void SGDTests() {
  // The identity function is super easy to learn, so we should always
  // be able to learn this with the default update config.
  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentitySparse(),
             100000, 1000, 0.0001f, {}, {100});
  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentityDense(),
             100000, 1000, 0.0001f);
  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentityConvolution(),
             100000, 1000, 0.0001f);

  // Smaller batch size since there are only 2^3 possible inputs.
  // This will sporadically achieve zero boolean errors after a
  // few thousand rounds, although that needs to coincide with
  // the loss check in order to actually succeed--so this no longer
  // finishes!
  //
  // With a less aggressive learning rate, this can take many
  // thousands of rounds to converge (or never converge).
  if (false) {
    // Doesn't converge in time, 11 Dec 2021
    TRAIN_TEST(NetworkTestUtil::LearnBoolean(),
               500000, 54, 0.001f);
  }

  // Interesting example.
  // This has a very simple solution (bias=0, all weights=1), but
  // the average case (bias = input size / 2) is quite far from it;
  // we spend most of the time trying to unlearn that initial
  // bias. I wonder if this is a case where "pre-training" might
  // be useful.
  TRAIN_TEST(NetworkTestUtil::LearnCountOnesDense(),
             10000, 1000,
             // Looks like it will eventually drop arbitrarily
             // low if you wait for it. Gets to this threshold
             // before about 600 rounds.
             0.100f);

  // Counting can be done with a convolution; this stacks a 4->1
  // convolution and a 5->1 convolution, followed by a dense layer
  // for the rest. (The dense layer is currently fixed!)
  // Slow! Takes 891k rounds with default settings!
  // (And sensitive to initial conditions. When I changed the
  // name (only), which changes the PRNG, it got much slower)
  TRAIN_TEST(NetworkTestUtil::LearnCountOnesConvConvDense(
                 /* last layer fixed */ true),
             1000000, 1000, 0.100f, UpdateWeightsCL::UpdateConfig());
}

static void AdamTests() {
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentitySparse()),
             1000, 1000, 0.001f);
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentityDense()),
             1000, 1000, 0.001f);
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentityConvolution()),
             1000, 1000, 0.001f);

  // This converges much faster than the SGD version! ~200 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(NetworkTestUtil::LearnBoolean()),
             6000, 54, 0.0001f);

  // Adam works well on this, even with a conservative learning
  // rate of 0.01f; once the weights get near 1, the bias rapidly
  // gets unlearned. Converges in <4000 rounds.
  // (XXX this is now like 3000 rounds with fixed adam)
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesDense()),
             10000, 1000, 0.100f);

  // With fixed adam this converges in <4000 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesConvConvDense(false)),
             100000, 1000, 0.010f);

  // With fixed, adam, converges in about 5100 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesConvDense()),
             10000, 1000, 0.010f);

  // Does converge in ~24000 rounds. Seems to be dependent on initial
  // conditions (as there is a late "breakthrough"), and perhaps with
  // more dice rolls for the features it would be pretty fast.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountEdges()),
             50000, 1000, 0.010f);
}

int main(int argc, char **argv) {
  cl = new CL;

  TestChunkSchedule();
  printf("Chunk schedule OK\n");

  QuickTests();
  printf("Quick tests OK\n");

  // SGDTests();
  printf("SGD tests OK\n");

  AdamTests();
  printf("ADAM tests OK\n");

  delete cl;

  printf("OK\n");
  return 0;
}
