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
#include "timer.h"

using namespace std;

// constexpr bool VERBOSE = true;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

struct TestResult {
  string name;
  string func;
  int line_number = 0;
  double seconds = 0.0;
  int64 rounds = 0;
  std::optional<string> error;
};

static std::vector<TestResult> *TestResults() {
  static std::vector<TestResult> *test_results =
    new std::vector<TestResult>;
  return test_results;
}
static void AddTestResult(const TestResult &tr) {
  TestResults()->push_back(tr);
}

static inline uint16_t PackHalf(half f) {
  static_assert(sizeof (half) == 2);
  uint16_t ret = 0;
  std::memcpy(&ret, &f, sizeof (half));
  return ret;
}

static void ForwardTests(TestNet test_net) {
  printf("\n--------------------------\n"
         "[Forward] Test net: %s\n", test_net.name.c_str());

  Network &net = test_net.net;
  auto net_gpu = make_unique<NetworkGPU>(cl, &net);
  net_gpu->SetVerbose(false);

  for (const TestExample &example : test_net.examples) {
    if (test_net.examples.size() > 1) printf("%s\n", example.name.c_str());
    std::unique_ptr<TrainingRoundGPU> training_round =
      std::make_unique<TrainingRoundGPU>(1, cl, net);

    training_round->LoadInput(0, FloatsToHalves(example.input));

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training_round.get(), src_layer);
    }

    // Must be initialized to the correct size.
    std::vector<half> gpu_out(net.layers.back().num_nodes, (half)-1.0f);
    training_round->ExportOutput(0, &gpu_out);

    CHECK_FEQV(HalvesToFloats(gpu_out), example.output);

    // And via the stimulation, which should be the same...
    Stimulation stim(net);
    training_round->ExportStimulation(0, &stim);
    // No change to input layer.
    CHECK_FEQV(HalvesToFloats(stim.values[0]), example.input);
    // Should have expected output after inference.
    CHECK_FEQV(HalvesToFloats(stim.values.back()), example.output);
  }
}


static void StructuralTests(TestNet test_net) {
  // TODO: More here.

  Network &net = test_net.net;
  auto net_gpu = make_unique<NetworkGPU>(cl, &net);
  net_gpu->SetVerbose(false);

  Network orig = net;
  vector<uint8> orig_vec = orig.Serialize();

  // Perturb.
  for (auto &layer : net.layers) {
    for (auto &chunk : layer.chunks) {
      for (half &f : chunk.weights) f += (half)1.0;
      for (half &f : chunk.biases) f -= (half)1.0;
    }
  }

  Network perturbed = net;
  vector<uint8> perturbed_vec = perturbed.Serialize();

  CHECK(orig_vec != perturbed_vec);

  net_gpu->WriteToGPU();

  // Clear
  for (auto &layer : net.layers) {
    for (auto &chunk : layer.chunks) {
      for (half &f : chunk.weights) f = (half)0.0;
      for (half &f : chunk.biases) f = (half)0.0;
    }
  }

  Network cleared = net;
  vector<uint8> cleared_vec = cleared.Serialize();
  CHECK(cleared_vec != perturbed_vec);
  CHECK(cleared_vec != orig_vec);

  // Read back the perturbed version.
  net_gpu->ReadFromGPU();
  Network perturbed2 = net;
  vector<uint8> perturbed2_vec = perturbed2.Serialize();
  CHECK(perturbed2_vec == perturbed_vec);
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
  net_gpu->SetVerbose(false);

  for (const TestExample &example : test_net.examples) {
    std::unique_ptr<TrainingRoundGPU> training_round =
      std::make_unique<TrainingRoundGPU>(1, cl, net);

    training_round->LoadInput(0, FloatsToHalves(example.input));

    // Perturb the training example so that it is not exactly what the
    // network already predicts, just so that the errors are not
    // trivial zeroes.
    vector<half> out = FloatsToHalves(example.output);
    for (half &f : out) {
      f += (half)gauss.Next();
    }

    training_round->LoadExpected(0, out);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training_round.get(), src_layer);
    }

    std::unique_ptr<SetOutputErrorCL> error_cl =
      std::make_unique<SetOutputErrorCL>(cl, net_gpu.get());

    error_cl->SetOutputError(training_round.get());

    std::unique_ptr<BackwardLayerCL> backward_cl =
      std::make_unique<BackwardLayerCL>(cl, net_gpu.get());

    for (int dst_layer = net.layers.size() - 1;
         // Note we propagate error to the input layer here,
         // which we expect to work, but is pointless during
         // training because there are no weights to update.
         dst_layer > 0;
         dst_layer--) {
      backward_cl->BackwardLayer(training_round.get(), dst_layer);
    }

    std::unique_ptr<DecayWeightsCL> decay_cl =
      std::make_unique<DecayWeightsCL>(cl, net_gpu.get(), 0.999999f);

    // Note that normally we would not decay weights on the input
    // layer (there are none), but we do it here to test that it does
    // not crash.
    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(layer_idx);
    }

    std::unique_ptr<UpdateWeightsCL> update_cl =
      std::make_unique<UpdateWeightsCL>(cl, net_gpu.get(),
                                        training_round->num_examples);

    for (int layer_idx = 1; layer_idx < net.layers.size(); layer_idx++) {
      update_cl->Update(training_round.get(), layer_idx);
    }
  }

  // Copy back to CPU instance.
  net_gpu->ReadFromGPU();
}

// Returns the count of rounds on success, and an optional error
// if training failed.
static std::pair<int64, std::optional<string>>
TrainTest(TrainNet train_net,
          int max_iterations,
          int examples_per_round,
          float avg_loss_threshold,
          UpdateWeightsCL::UpdateConfig update_config =
          UpdateWeightsCL::UpdateConfig(),
          optional<int> write_images_every = nullopt,
          // Output indexes to graph error for. This happens
          // whenever we compute error.
          vector<int> write_error_images_for = {}) {
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = false;
  static constexpr int MAX_PARALLELISM = 4;

  std::unique_ptr<TrainingImages> images;
  if (write_images_every.has_value()) {
    images.reset(new TrainingImages(train_net.net, "test", train_net.name,
                                    write_images_every.value(), 3000, 1000,
                                    // don't continue from disk, since
                                    // these may not even be the same
                                    // problem!
                                    false));
  }

  struct ErrorImageRow {
    int col = 0;
    std::unique_ptr<ErrorImage> image;
    // Temporary scratch space.
    vector<pair<float, float>> ex;
  };

  vector<ErrorImageRow> error_images(write_error_images_for.size());
  for (int idx = 0; idx < write_error_images_for.size(); idx++) {
    const int col = write_error_images_for[idx];
    error_images[idx].col = col;
    error_images[idx].image = std::make_unique<ErrorImage>(
        2000, examples_per_round, StringPrintf("test-error-%d.png", col),
        false);
  }

  printf("\n--------------------------\n"
         "[Train] Train net: %s\n", train_net.name.c_str());
  ArcFour rc(train_net.name + "XXX");
  RandomGaussian gauss(&rc);

  Network &net = train_net.net;
  // Initialize with random weights.
  RandomizeNetwork(&rc, &net, 2);

  auto net_gpu = make_unique<NetworkGPU>(cl, &net);
  net_gpu->SetVerbose(false);

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net_gpu.get());
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net_gpu.get());
  [[maybe_unused]]
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net_gpu.get(), 0.999999999f);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(
        cl, net_gpu.get(), examples_per_round, update_config);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(examples_per_round, cl, net));

  // Used to compute loss.
  std::vector<std::vector<half>> expected;
  expected.resize(training->num_examples);

  // Also used in error message if we don't converge.
  float average_loss = 0.0f;
  float average_inc = 0.0f;


  Timer train_timer;
  int64 total_examples = 0LL;
  for (int iter = 0; iter < max_iterations; iter++) {

    // Initialize training examples as a batch.
    std::vector<half> flat_inputs, flat_outputs;
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
      for (float f : inputs) flat_inputs.push_back((half)f);
      std::vector<float> outputs = train_net.f(inputs);
      CHECK(outputs.size() == train_net.NumOutputs());
      for (float f : outputs) flat_outputs.push_back((half)f);
      // XXX could probably save these as floats?
      expected[i] = FloatsToHalves(outputs);
    }

    training->LoadInputs(flat_inputs);
    training->LoadExpecteds(flat_outputs);

    if (VERBOSE > 1)
      printf("Prepped examples.\n");

    for (int src_layer = 0;
         src_layer < net.layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training.get(), src_layer);
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    error_cl->SetOutputError(training.get());

    if (VERBOSE > 1)
      printf("Set error.\n");

    for (int dst_layer = net.layers.size() - 1;
         // Don't propagate to input.
         dst_layer > 1;
         dst_layer--) {
      backward_cl->BackwardLayer(training.get(), dst_layer);
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    if (false)  // XXX
    for (int layer_idx = 0; layer_idx < net.layers.size(); layer_idx++) {
      decay_cl->Decay(layer_idx);
    }

    // PERF: No benefit to parallelism here currently, as each takes
    // the global mutex. A future version might, though.
    ParallelComp(net.layers.size() - 1,
                 [&](int layer_minus_1) {
                   const int layer_idx = layer_minus_1 + 1;
                   update_cl->Update(training.get(), layer_idx);
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

      // Populated for error images.
      for (int cidx = 0; cidx < error_images.size(); cidx++) {
        ErrorImageRow *row = &error_images[cidx];
        row->ex.resize(examples_per_round);
      }

      // Get loss as abs distance, plus number of incorrect (as booleans).
      // Size of examples = Number of training instances.
      std::vector<std::pair<float, int>> losses =
        ParallelMapi(expected,
                     [&](int idx, const std::vector<half> &exp) {
                       // Loops over every example and gets its data.
                       std::vector<half> got;
                       got.resize(exp.size());
                       training->ExportOutput(idx, &got);

                       // But the error images are organized by column.
                       for (int cidx = 0; cidx < error_images.size(); cidx++) {
                         ErrorImageRow *row = &error_images[cidx];
                         CHECK(idx < row->ex.size());
                         row->ex[idx].first = exp[cidx];
                         row->ex[idx].second = got[cidx];
                       }

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

      for (ErrorImageRow &row : error_images) {
        row.image->Add(std::move(row.ex));
        if ((iter < 10000 && iter % 1000 == 0) ||
            iter % 10000 == 0) {
          row.image->Save();
        }
      }

      if (VERBOSE > 1)
        printf("Got losses.\n");

      float min_loss = 1.0f / 0.0f, max_loss = 0.0f;
      int min_inc = net.layers.back().num_nodes + 1, max_inc = 0;
      average_loss = 0.0f;
      average_inc = 0.0f;
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
      // XXXXX

      CHECK(net.layers.size() == 2);
      CHECK(net.layers[1].chunks.size() == 1);
      const Chunk &chunk = net.layers[1].chunks[0];
      printf("Weight: %04x = %.6f Bias: %04x = %.6f\n",
             PackHalf(chunk.weights[0]),
             (float)chunk.weights[0],
             PackHalf(chunk.biases[0]),
             (float)chunk.biases[0]);

      #if 0
      for (int i = 0; i < 10 && i < examples_per_round; i++) {
        Errors err(net);
        training->ExportErrors(i, &err);
        CHECK(err.error.size() == 2);
        CHECK(err.error[1].size() == 1);
        const half h = err.error[1][0];
        printf("%04x = %.6f, ", PackHalf(h), (float)h);
      }
      printf("\n");
      #endif
    }

    if (SAVE_INTERMEDIATE && (finished || iter == 1000 || iter % 10000 == 0)) {
      net_gpu->ReadFromGPU();
      const string file = StringPrintf("gpu-test-net-%d.val", iter);
      net.SaveToFile(file);
      if (VERBOSE)
        printf("Wrote %s\n", file.c_str());
    }

    // Parameter for average_loss termination?
    if (finished) {
      for (ErrorImageRow &row : error_images) row.image->Save();
      printf("Successfully trained!\n");
      return std::make_pair(iter, std::nullopt);
    }
  }

  return std::make_pair(
      max_iterations,
      StringPrintf("didn't converge. error still %.6f",
                   max_iterations));
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

  StructuralTests(NetworkTestUtil::SingleSparse());
  StructuralTests(NetworkTestUtil::SingleDense());
  StructuralTests(NetworkTestUtil::SingleConvolution());
  StructuralTests(NetworkTestUtil::TwoInputSparse());
  StructuralTests(NetworkTestUtil::TwoDenseChunks());
  StructuralTests(NetworkTestUtil::Net1());
  StructuralTests(NetworkTestUtil::SimpleConv());
  StructuralTests(NetworkTestUtil::CountInternalEdges());

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
#define TRAIN_TEST(args...) do {                        \
  Timer test_timer;                                     \
  const auto &[r, eo] = TrainTest(args);                \
    TestResult result{.name = #args ,                   \
                      .func = __func__,                 \
                      .line_number = __LINE__,          \
                      .seconds = test_timer.Seconds(),  \
                      .rounds = r,                      \
                      .error = eo};                     \
    AddTestResult(result);                              \
    if (eo.has_value()) {                               \
      LOG(INFO) << "In "                                \
        << __func__                                     \
        << ":" << __LINE__ << "\n"                      \
        << "Train test failed: "                        \
        << #args                                        \
        << "\nWith message:\n"                          \
                << eo.value();                          \
    }                                                   \
  } while(0)


// Test SGD learning algorithm, which is generally worse than ADAM but
// is easier to understand. It has less train-time overhead.
static void SGDTests() {
  // The identity function is super easy to learn, so we should always
  // be able to learn this with the default update config.

  #if 0
  UpdateWeightsCL::UpdateConfig zz_config = UpdateWeightsCL::UpdateConfig{
    .base_learning_rate = 0.2f,
    .learning_rate_dampening = 1.0f,
    .adam_epsilon = 1.0e-6,
  };
#endif

  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentitySparse(),
             200000, 4000, 0.001f, {}, {100}, {0});

  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentityDense(),
             200000, 1000, 0.001f);
  TRAIN_TEST(NetworkTestUtil::LearnTrivialIdentityConvolution(),
             200000, 1000, 0.001f);

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

  [[maybe_unused]]
  UpdateWeightsCL::UpdateConfig fast_config = UpdateWeightsCL::UpdateConfig{
    .base_learning_rate = 0.1f,
    .learning_rate_dampening = 0.25f,
    .adam_epsilon = 1.0e-6,
  };

  // Interesting example.
  // This has a very simple solution (bias=0, all weights=1), but
  // the average case (bias = input size / 2) is quite far from it;
  // we spend most of the time trying to unlearn that initial
  // bias. I wonder if this is a case where "pre-training" might
  // be useful.
  TRAIN_TEST(NetworkTestUtil::LearnCountOnesDense(),
             20000, 1000,
             // Looks like it will eventually drop arbitrarily
             // low if you wait for it. Gets to this threshold
             // before about 600 rounds.
             0.100f,
             fast_config);

  // Counting can be done with a convolution; this stacks a 4->1
  // convolution and a 5->1 convolution, followed by a dense layer
  // for the rest. (The dense layer is currently fixed!)
  // Slow! Takes 891k rounds with default settings!
  // (And sensitive to initial conditions. When I changed the
  // name (only), which changes the PRNG, it got much slower)
  if (false /* too slow! */) {
    TRAIN_TEST(NetworkTestUtil::LearnCountOnesConvConvDense(
                   /* last layer fixed */ true),
               1000000, 1000, 0.100f, UpdateWeightsCL::UpdateConfig());
  }
}

static void AdamTests() {
  [[maybe_unused]]
  UpdateWeightsCL::UpdateConfig fast_config = UpdateWeightsCL::UpdateConfig{
    .base_learning_rate = 0.1f,
    .learning_rate_dampening = 0.25f,
    .adam_epsilon = 1.0e-6,
  };

  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentitySparse()),
             10000, 1000, 0.001f, fast_config);
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentityDense()),
             10000, 1000, 0.001f, fast_config);
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnTrivialIdentityConvolution()),
             10000, 1000, 0.001f, fast_config);

  // This converges much faster than the SGD version! ~200 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(NetworkTestUtil::LearnBoolean()),
             6000, 54, 0.0001f, fast_config);

  // Adam works well on this, even with a conservative learning
  // rate of 0.01f; once the weights get near 1, the bias rapidly
  // gets unlearned. Converges in <4000 rounds.
  // (XXX this is now like 3000 rounds with fixed adam)
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesDense()),
             10000, 1000, 0.100f, fast_config);

  // With fixed adam this converges in <4000 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesConvConvDense(false)),
             100000, 1000, 0.010f, fast_config);

  // With fixed, adam, converges in about 5100 rounds.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountOnesConvDense()),
             10000, 1000, 0.010f, fast_config);

  // Does converge in ~24000 rounds. Seems to be dependent on initial
  // conditions (as there is a late "breakthrough"), and perhaps with
  // more dice rolls for the features it would be pretty fast.
  TRAIN_TEST(NetworkTestUtil::ForceAdam(
                 NetworkTestUtil::LearnCountEdges()),
             50000, 1000, 0.010f, fast_config);

  TRAIN_TEST(NetworkTestUtil::TriangleSumsAdam(3, 4),
             5000, 1000, 0.010f, fast_config);

  // Deep!
  TRAIN_TEST(NetworkTestUtil::TriangleSumsAdam(9, 5),
             20000, 1000, 0.010f, fast_config);

  // 4000 rounds...! Is it just lucky?
  TRAIN_TEST(NetworkTestUtil::SparseInCircleAdam(128, 16, 12),
             20000, 1000, 0.010f, fast_config);

  // About 16k rounds
  TRAIN_TEST(NetworkTestUtil::Atan2Adam(64, 6, 6),
             50000, 1000, 0.010f, fast_config);


  // Open question: Why do weights move so slowly when the
  // network is wide? The magnitudes start smaller at
  // initialization time? Errors tend to average out, so
  // the gradient is very flat? Maybe TrainingImages just
  // is bad at drawing it (doesn't seem so?)

  if (false) {
    // This does converge in about 650k rounds. Slow!
    TRAIN_TEST(NetworkTestUtil::InCircleAdam(4, 4),
               2000000, 1000, 0.010f, fast_config, {100});
  }

  // 116k rounds
#if 0
  TRAIN_TEST(NetworkTestUtil::SparseInCircleAdam(128, 2, 3),
             2000000, 1000, 0.010f, fast_config, {100});
#endif

  // 15k rounds
  #if 0
  TRAIN_TEST(NetworkTestUtil::SparseInCircleAdam(128, 2, 6),
             2000000, 1000, 0.010f, fast_config, {100});
#endif


  {
    UpdateWeightsCL::UpdateConfig hyper_config =
      UpdateWeightsCL::UpdateConfig{
      .base_learning_rate = 0.049972f,
      .learning_rate_dampening = 4.689531f,
      .adam_epsilon = 0.00001921458f,
    };

    // The resulting model is still pretty noisy (it does not get
    // exact answers), but is qualitatively pretty decent.
    TRAIN_TEST(NetworkTestUtil::SparseLineIntersectionAdam(183, 42, 6),
               200000, 1000, 0.060f, hyper_config);
  }

  // Tests tanh transfer function. Should be able to learn this simple
  // function easily. Still it takes 40k rounds, perhaps because the
  // learning rates are too conservative?
  {
    UpdateWeightsCL::UpdateConfig tanh_config = fast_config;
    // For this problem, the solution is weights towards infinity. So
    // don't constrain them too much.
    tanh_config.weight_constrain_max = 1e5;

    TRAIN_TEST(NetworkTestUtil::TanhSignFunctionAdam(),
               100000, 1000, 0.025f, tanh_config, {100}, {0});
  }
}

static void AuditionTests() {
  [[maybe_unused]]
  UpdateWeightsCL::UpdateConfig fast_config = UpdateWeightsCL::UpdateConfig{
    .base_learning_rate = 0.1f,
    .learning_rate_dampening = 0.25f,
    .adam_epsilon = 1.0e-6,
  };


  [[maybe_unused]]
  UpdateWeightsCL::UpdateConfig slower_config = UpdateWeightsCL::UpdateConfig{
    .base_learning_rate = 0.01f,
    .learning_rate_dampening = 0.25f,
    .adam_epsilon = 1.0e-5,
  };


  // shows divergence after convergence
  // TRAIN_TEST(NetworkTestUtil::SparseLineIntersectionAdam(128, 16, 12),
  // 2000000, 1000, 0.010f, fast_config);

  // also diverges quickly
  // TRAIN_TEST(NetworkTestUtil::SparseLineIntersectionAdam(256, 16, 6),
  // 2000000, 1000, 0.010f, fast_config, {100});


  #if 0
  TRAIN_TEST(NetworkTestUtil::SparseLineIntersectionAdam(128, 6, 6),
             2000000, 1000, 0.010f, slower_config);
  #endif

  #if 0
  TRAIN_TEST(NetworkTestUtil::ReflectAdam(128, 6, 6),
             2000000, 1000, 0.010f, fast_config);
  #endif

  // diverges, although not wildly
  // TRAIN_TEST(NetworkTestUtil::DodgeballAdam(113, 21, 4),
  //    2000000, 1000, 0.010f, fast_config, {100});

  #if 0
  // gets under 0.04 in 700k rounds, perhaps keeps going
  TRAIN_TEST(NetworkTestUtil::DodgeballAdam(113, 21, 4),
             2000000, 1000, 0.040f, slower_config, {100});
  #endif
}

int main(int argc, char **argv) {
  cl = new CL;

  Timer timer;

  TestChunkSchedule();
  printf("Chunk schedule OK\n");

  QuickTests();
  printf("Quick tests OK\n");

  SGDTests();
  printf("SGD tests OK\n");

  AdamTests();
  printf("ADAM tests OK\n");

  // not permanent...
  AuditionTests();

  delete cl;

  auto MinSec = [](double sec) {
      int min = (int)sec / 60;
      double sr = sec - (60.0 * min);
      if (min > 0) {
        return StringPrintf("%dm%.1fs", min, sr);
      } else {
        return StringPrintf("%.3fs", sec);
      }
    };
  for (const TestResult &tr : *TestResults()) {
    printf("%s\n"
           "At %s:%d. Took %lld iters, %s.\n",
           tr.name.c_str(),
           tr.func.c_str(), tr.line_number,
           tr.rounds, MinSec(tr.seconds).c_str());
    if (tr.error.has_value()) {
      printf("** FAIL: %s **\n",
             tr.error.value().c_str());
    }
  }

  printf("Finished all tests in %s\n",
         MinSec(timer.Seconds()).c_str());

  printf("OK\n");
  return 0;
}
