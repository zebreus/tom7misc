
// Trains a network for the standard CIFAR10 image classification test
// problem.

#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <chrono>
#include <thread>
#include <deque>
#include <numbers>

#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "image.h"
#include "util.h"
#include "train-util.h"
#include "periodically.h"
#include "timer.h"
#include "error-history.h"
#include "ansi.h"

#include "eval-cifar10.h"
#include "cifar10.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

using int64 = int64_t;

static constexpr WeightUpdate WEIGHT_UPDATE = ADAM;

#define MODEL_BASE "classify"
#define MODEL_NAME MODEL_BASE ".val"

// Known dimensions for CIFAR-10 training data.
static constexpr int IMG_WIDTH = CIFAR10::WIDTH;
static constexpr int IMG_HEIGHT = CIFAR10::HEIGHT;

constexpr int IMG_CHANNELS = 3;
constexpr int INPUT_SIZE = IMG_WIDTH * IMG_HEIGHT * IMG_CHANNELS;
constexpr int OUTPUT_SIZE = CIFAR10::RADIX;

// Very small examples -- this can probably be much higher.
static constexpr int EXAMPLES_PER_ROUND = 1024;

// XXX to train-util etc.
struct TrainParams {
  UpdateConfig update_config = {};

  bool do_decay = false;
  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  float decay_rate = 0.999999f;

  string ToString() const {
    return StringPrintf("{.update_config = %s, "
                        ".do_decay = %s, "
                        ".decay_rate = %.11g}",
                        update_config.ToString().c_str(),
                        do_decay ? "true" : "false",
                        decay_rate);
  }
};

static TrainParams DefaultParams() {
  TrainParams tparams;
  // These are rounded versions of the optimized TANH from
  // learn-chess; not tested. (It worked fine for 200k rounds
  // with leaky-relu, although it still appeared to be getting
  // better when it stopped.)
  tparams.update_config.base_learning_rate = 0.005f;
  tparams.update_config.learning_rate_dampening = 13.0f;
  tparams.update_config.adam_epsilon = 1.0e-6;
  tparams.update_config.constrain = true;
  tparams.update_config.weight_constrain_max = 3.0f;
  tparams.update_config.bias_constrain_max = 8760.0f;
  tparams.update_config.clip_error = true;
  tparams.update_config.error_max = 1000.0f;

  tparams.do_decay = false;
  return tparams;
}

struct ExampleThread {
  // A full round's worth of examples.
  // First element is INPUT_SIZE * EXAMPLES_PER_ROUND,
  // then OUTPUT_SIZE * EXAMPLES_PER_ROUND.
  using example_type = std::pair<std::vector<float>, std::vector<float>>;

  example_type GetExamples() {
    for (;;) {
      {
        MutexLock ml(&m);
        if (!q.empty()) {
          example_type ret = std::move(q.front());
          q.pop_front();
          return ret;
        }
      }

      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  ExampleThread() {
    cifar10.reset(new CIFAR10("cifar10", false));
    CHECK(cifar10.get() != nullptr);

    work_thread1.reset(new std::thread(&Generate, this, 1));
    work_thread2.reset(new std::thread(&Generate, this, 2));
  }

  ~ExampleThread() {
    {
      MutexLock ml(&m);
      examplethread_should_die = true;
    }
    work_thread1->join();
    work_thread2->join();
  }

private:
  static constexpr int TARGET_SIZE = 10;

  void Generate(int id) {
    printf("Started example thread %d.\n", id);

    // Not deterministic.
    ArcFour rc(StringPrintf("%d.%lld.ex", id, time(nullptr)));
    RandomGaussian gauss(&rc);

    for (;;) {
      const auto [want_more, should_die] = [&]() -> pair<bool, bool> {
          MutexLock ml(&m);
          return make_pair(q.size() < TARGET_SIZE, examplethread_should_die);
        }();

      if (should_die)
        return;

      if (want_more) {
        // examples_per_round * INPUT_SIZE
        std::vector<float> examples;
        // examples_per_round * OUTPUT_SIZE
        std::vector<float> outputs;
        examples.reserve(EXAMPLES_PER_ROUND * INPUT_SIZE);
        outputs.reserve(EXAMPLES_PER_ROUND * OUTPUT_SIZE);

        // Fill 'em
        for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {

          const int idx = RandTo(&rc, cifar10->Num());
          CHECK(idx < cifar10->labels.size() &&
                idx < cifar10->images.size());
          // one-hot output label
          const int lab = cifar10->labels[idx];
          for (int o = 0; o < OUTPUT_SIZE; o++) {
            outputs.push_back(lab == o ? 1.0f : 0.0f);
          }

          // image data.
          const ImageRGBA &img = cifar10->images[idx];

          // Shift by up to two pixels in each direction.
          const int dx = RandTo(&rc, 5) - 2;
          const int dy = RandTo(&rc, 5) - 2;
          // And add a little gaussian noise.
          static constexpr float NOISE_SCALE = 0.025f;
          for (int y = 0; y < IMG_HEIGHT; y++) {
            // Copy pixels from border
            int yy = std::clamp(y + dy, 0, IMG_HEIGHT - 1);
            for (int x = 0; x < IMG_WIDTH; x++) {
              int xx = std::clamp(x + dx, 0, IMG_WIDTH - 1);
              auto [r, g, b, a_] = img.GetPixel(xx, yy);
              examples.push_back(r * (1.0f / 255.0f) +
                                 gauss.Next() * NOISE_SCALE);
              examples.push_back(g * (1.0f / 255.0f) +
                                 gauss.Next() * NOISE_SCALE);
              examples.push_back(b * (1.0f / 255.0f) +
                                 gauss.Next() * NOISE_SCALE);
            }
          }
        }

        CHECK(examples.size() == EXAMPLES_PER_ROUND * INPUT_SIZE);
        CHECK(outputs.size() == EXAMPLES_PER_ROUND * OUTPUT_SIZE);

        {
          MutexLock ml(&m);
          q.emplace_back(std::move(examples), std::move(outputs));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
  }

  std::unique_ptr<CIFAR10> cifar10;

  std::mutex m;
  // queue of examples, ready to train
  bool examplethread_should_die = false;
  std::deque<example_type> q;

  std::unique_ptr<std::thread> work_thread1, work_thread2;
};

static void Train(const string &dir, Network *net, int64 max_rounds,
                  TrainParams params) {
  ExampleThread example_thread;

  const string error_history_file = Util::dirplus(dir, "error-history.tsv");
  const string model_file = Util::dirplus(dir, MODEL_NAME);

  ErrorHistory error_history(error_history_file);

  std::array<std::string, CIFAR10::RADIX> labels;
  for (int i = 0; i < CIFAR10::RADIX; i++)
    labels[i] = CIFAR10::LabelString(i);
  ConfusionImage<CIFAR10::RADIX> conf_image(
      1920, labels, Util::dirplus(dir, "conf.png"));

  EvalCIFAR10 evaluator(cl);

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;

  // On a verbose round we compute training error and print out
  // examples.
  constexpr int VERBOSE_EVERY = 1000;
  int64 total_verbose = 0;

  // How often to also save error history to disk (if we run a
  // verbose round). This also evaluates on the test data set
  // simultaneously.
  //
  // Early in training, we do this more often, as the changes
  // are often rapid then.
  const double HISTORY_EVERY_SEC =
    net->rounds < 1000 ? 30.0 : 120.0;
  Periodically history_per(HISTORY_EVERY_SEC);

  static constexpr double SAVE_ERROR_IMAGES_EVERY = 130.0;
  Periodically error_images_per(SAVE_ERROR_IMAGES_EVERY);

  static constexpr double TIMING_EVERY_SEC = 60.0;
  Periodically timing_per(TIMING_EVERY_SEC);


  static constexpr int64 CHECKPOINT_EVERY_ROUNDS = 100000;

  constexpr int IMAGE_EVERY = 100;
  TrainingImages images(*net, Util::dirplus(dir, "train"),
                        model_file, IMAGE_EVERY);

  printf("Training!\n");

  constexpr int NUM_COLUMNS = CIFAR10::RADIX;
  std::vector<std::unique_ptr<ErrorImage>> error_images;
  error_images.reserve(NUM_COLUMNS);
  for (int i = 0; i < NUM_COLUMNS; i++) {
    string filename = Util::dirplus(dir, StringPrintf("error-%d.png", i));
    error_images.emplace_back(
        std::make_unique<ErrorImage>(2000, EXAMPLES_PER_ROUND, filename,
                                     true));
  }
  CHECK(error_images.size() == NUM_COLUMNS);

  if (VERBOSE > 1)
    printf("Loaded/created error images.\n");

  auto net_gpu = make_unique<NetworkGPU>(cl, net);

  if (VERBOSE > 1)
    printf("Net on GPU.\n");

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net_gpu.get(),
                                       params.update_config);
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net_gpu.get(),
                                      params.update_config);
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net_gpu.get(),
                                     params.decay_rate);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, net_gpu.get(),
                                      EXAMPLES_PER_ROUND,
                                      params.update_config);

  if (VERBOSE > 1)
    printf("Compiled CL.\n");

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));

  CHECK(!net->layers.empty());
  CHECK(net->layers[0].num_nodes == INPUT_SIZE);
  CHECK(net->layers.back().num_nodes == OUTPUT_SIZE);

  if (VERBOSE > 1)
    printf("Training round ready.\n");

  double round_ms = 0.0;
  double getexample_ms = 0.0;
  double example_ms = 0.0;
  double forward_ms = 0.0;
  double error_ms = 0.0;
  double decay_ms = 0.0;
  double backward_ms = 0.0;
  double update_ms = 0.0;
  double loss_ms = 0.0;
  double image_ms = 0.0;

  Timer train_timer;
  int64 total_examples = 0LL;
  // seconds since timer started
  double last_save = 0.0;
  for (int iter = 0; net->rounds < max_rounds; iter++) {
    Timer round_timer;

    const bool verbose_round = (iter % VERBOSE_EVERY) == 0;

    Timer getexample_timer;
    // All examples for the round, flat.
    auto [inputs, expecteds] = example_thread.GetExamples();

    getexample_ms += getexample_timer.MS();

    {
      Timer example_timer;
      CHECK(inputs.size() == INPUT_SIZE * EXAMPLES_PER_ROUND);
      CHECK(expecteds.size() == OUTPUT_SIZE * EXAMPLES_PER_ROUND);
      training->LoadInputs(inputs);
      training->LoadExpecteds(expecteds);
      example_ms += example_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Prepped examples.\n");

    {
      Timer forward_timer;
      for (int src_layer = 0;
           src_layer < net->layers.size() - 1;
           src_layer++) {
        forward_cl->RunForward(training.get(), src_layer);
      }
      forward_ms += forward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    {
      Timer error_timer;
      error_cl->SetOutputError(training.get());
      error_ms += error_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Set error.\n");

    {
      Timer backward_timer;
      for (int dst_layer = net->layers.size() - 1;
           // Don't propagate to input.
           dst_layer > 1;
           dst_layer--) {
        backward_cl->BackwardLayer(training.get(), dst_layer);
      }
      backward_ms += backward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    if (params.do_decay) {
      Timer decay_timer;
      for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
        decay_cl->Decay(layer_idx);
      }
      decay_ms += decay_timer.MS();
    }

    {
      Timer update_timer;
      // Can't run training examples in parallel because these all write
      // to the same network. But each layer is independent.
      UnParallelComp(net->layers.size() - 1,
                     [&](int layer_minus_1) {
                       const int layer_idx = layer_minus_1 + 1;
                       update_cl->Update(training.get(),
                                         layer_idx);
                     },
                     max_parallelism);
      update_ms += update_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Updated weights.\n");

    total_examples += EXAMPLES_PER_ROUND;

    net->examples += EXAMPLES_PER_ROUND;
    net->rounds++;

    // (currently no way to actually finish, but we could set a
    // training error threshold below.)
    const bool finished = false;

    if (verbose_round) {
      Timer loss_timer;
      if (VERBOSE > 1)
        printf("Verbose round...\n");

      const double total_sec = train_timer.Seconds();
      const double eps = total_examples / total_sec;
      const double rps = iter / (double)total_sec;

      const int rounds_left = max_rounds - net->rounds;
      const double sec_left = rounds_left / rps;

      printf(AYELLOW("%lld") " rounds "
             " | " ABLUE("%.2f") " rounds/min;  "
             APURPLE("%.2f") " eps | ~" ACYAN("%.3f") " hours left\n",
             net->rounds, rps * 60.0, eps,
             sec_left / (60.0 * 60.0));

      // Get loss as abs distance, plus number of incorrect (as booleans).

      // PERF could do this on the flat vector, but we only need to
      // run it for verbose rounds
      std::vector<std::vector<float>> expected;
      expected.reserve(EXAMPLES_PER_ROUND);
      for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
        std::vector<float> one;
        one.resize(OUTPUT_SIZE, 0.0f);
        for (int j = 0; j < OUTPUT_SIZE; j++) {
          one[j] = expecteds[i * OUTPUT_SIZE + j];
        }
        expected.emplace_back(std::move(one));
      }

      if (VERBOSE > 1)
        printf("Got expected\n");

      // Get loss as abs distance, plus number of incorrect (as booleans).
      std::vector<float> actuals(EXAMPLES_PER_ROUND * OUTPUT_SIZE);
      training->ExportOutputs(&actuals);

      const bool save_history = history_per.ShouldRun();
      const bool save_error_images = error_images_per.ShouldRun();
      // compute loss for each digit separately

      double overall_average_loss = 0.0;
      for (int col = 0; col < NUM_COLUMNS; col++) {

        std::vector<std::pair<float, float>> ex(EXAMPLES_PER_ROUND);
        float average_loss = 0, min_loss = 1.0f / 0.0f, max_loss = 0.0f;
        for (int idx = 0; idx < EXAMPLES_PER_ROUND; idx++) {
          float expected = expecteds[idx * NUM_COLUMNS + col];
          float actual = actuals[idx * NUM_COLUMNS + col];
          if (idx == 0) {
            printf("%+.2f%c ", actual, expected > 0.5f ? '<' : ' ');
          }
          ex[idx] = std::make_pair(expected, actual);
          float loss = fabsf(expected - actual);
          min_loss = std::min(min_loss, loss);
          max_loss = std::max(max_loss, loss);
          average_loss += loss;
        }

        average_loss /= EXAMPLES_PER_ROUND;
        overall_average_loss += average_loss;

        error_images[col]->Add(std::move(ex));

        if (save_error_images) {
          error_images[col]->Save();
          printf("Wrote %s\n", error_images[col]->Filename().c_str());
        }

        printf("   %c: %.3f<%.3f<%.3f\n",
               '0' + col,
               min_loss, average_loss, max_loss);
      }

      overall_average_loss /= NUM_COLUMNS;
      printf("overall: %.4f\n", overall_average_loss);
      if (save_history) {
        net_gpu->ReadFromGPU();
        EvalCIFAR10::Result res = evaluator.Evaluate(net);
        EvalCIFAR10::TitleResult(&res);
        res.wrong.Save(Util::dirplus(dir, "test-wrong.png"));
        double test_loss = (res.total - res.correct) / (double)res.total;
        error_history.Add(net->rounds, overall_average_loss,
                          ErrorHistory::ERROR_TRAIN);
        error_history.Add(net->rounds, test_loss,
                          ErrorHistory::ERROR_TEST);
        printf("Test loss: %.4f in %.4fs\n", test_loss, res.fwd_time);
        error_history.MakeImage(
            1920, 1080,
            {{ErrorHistory::ERROR_TRAIN, 0x0033FFFF},
             {ErrorHistory::ERROR_TEST, 0x00FF00FF}},
            0).Save(Util::dirplus(dir, "error-history.png"));
        conf_image.Add(res.conf);
        conf_image.Save();

        if (net->rounds > 1000)
          history_per.SetPeriod(120.0);
      }

      total_verbose++;
      loss_ms += loss_timer.MS();
    }

    // TODO: Should probably synchronize saving images with saving
    // the model. Otherwise when we continue, we lose pixels that
    // were written to the image but not saved to disk. We now explicitly
    // save when we save the image,
    if (net->rounds < IMAGE_EVERY || (iter % IMAGE_EVERY) == 0) {
      Timer image_timer;
      net_gpu->ReadFromGPU();
      const vector<vector<float>> stims = training->ExportStimulationsFlat();
      images.Sample(*net, EXAMPLES_PER_ROUND, stims);
      image_ms += image_timer.MS();
    }

    static constexpr double SAVE_EVERY_SEC = 180.0;
    bool save_timeout = false;
    if ((train_timer.MS() / 1000.0) > last_save + SAVE_EVERY_SEC) {
      save_timeout = true;
      last_save = train_timer.MS() / 1000.0;
    }

    // Checkpoint (no overwrite) every X rounds.
    bool checkpoint_timeout = (net->rounds % CHECKPOINT_EVERY_ROUNDS) == 0;

    if (SAVE_INTERMEDIATE && (save_timeout || checkpoint_timeout || finished ||
                              iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      // Note that if we write a checkpoint, we don't also overwrite
      // the main model, which is less redundant but might be surprising?
      const string filename = checkpoint_timeout ?
        StringPrintf("%s.%lld.val",
                     Util::dirplus(dir, MODEL_BASE).c_str(),
                     net->rounds) : model_file;
      net->SaveToFile(filename);
      if (VERBOSE)
        printf("Wrote %s\n", filename.c_str());
      error_history.Save();
      images.Save();
    }

    // Parameter for average_loss termination?
    if (finished) {
      printf("Successfully trained!\n");
      return;
    }

    round_ms += round_timer.MS();

    if (timing_per.ShouldRun()) {
      double accounted_ms = getexample_ms + example_ms + forward_ms +
        error_ms + decay_ms + backward_ms + update_ms + loss_ms +
        image_ms;
      double other_ms = round_ms - accounted_ms;
      double pct = 100.0 / round_ms;
      printf("%.1f%% ge  "
             "%.1f%% ex  "
             "%.1f%% fwd  "
             "%.1f%% err  "
             "%.1f%% dec  "
             "%.1f%% bwd  "
             "%.1f%% up  "
             "%.1f%% loss "
             "%.1f%% img "
             "%.1f%% other\n",
             getexample_ms * pct,
             example_ms * pct,
             forward_ms * pct,
             error_ms * pct,
             decay_ms * pct,
             backward_ms * pct,
             update_ms * pct,
             loss_ms * pct,
             image_ms * pct,
             other_ms * pct);
      double msr = 1.0 / (iter + 1);
      printf("%.1fms ge  "
             "%.1fms ex  "
             "%.1fms fwd  "
             "%.1fms err  "
             "%.1fms dec  "
             "%.1fms bwd  "
             "%.1fms up  "
             "%.2fms loss  "
             "%.2fms img  "
             "%.1fms other\n",
             getexample_ms * msr,
             example_ms * msr,
             forward_ms * msr,
             error_ms * msr,
             decay_ms * msr,
             backward_ms * msr,
             update_ms * msr,
             loss_ms * msr,
             image_ms * msr,
             other_ms * msr);
    }
  }

  error_history.Save();
  images.Save();

  printf("Reached max_rounds of %lld.\n", max_rounds);
  net->SaveToFile(model_file);
  printf("Saved to %s.\n", model_file.c_str());
}

static unique_ptr<Network> NewImagesNetwork(TransferFunction tf) {
  printf("New network with transfer function %s\n",
         TransferFunctionName(tf));

  // Deterministic!
  ArcFour rc("learn-images-network");



  std::vector<Layer> layers;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = IMG_WIDTH;
  input_chunk.height = IMG_HEIGHT;
  input_chunk.channels = IMG_CHANNELS;

  layers.push_back(Network::LayerFromChunks(input_chunk));

  // RGB
  const int CHANNELS = 3;
  // i.e. 3x3 pixels
  const int CONV1_SIZE = 3;
  const int CONV1_FEATURES = 64;

  Chunk first_conv_chunk =
    Network::Make2DConvolutionChunk(
        // Entire image
        0, IMG_WIDTH * CHANNELS, IMG_HEIGHT,
        CONV1_FEATURES, CONV1_SIZE * CHANNELS, CONV1_SIZE,
        // full overlap
        CHANNELS, 1,
        tf, WEIGHT_UPDATE);

  const int CONV2_SIZE = 8;
  const int CONV2_FEATURES = 256;

  Chunk second_conv_chunk =
    Network::Make2DConvolutionChunk(
        // Entire image
        0, IMG_WIDTH * CHANNELS, IMG_HEIGHT,
        CONV2_FEATURES, CONV2_SIZE * CHANNELS, CONV2_SIZE,
        // full overlap
        CHANNELS, 1,
        tf, WEIGHT_UPDATE);

  layers.push_back(Network::LayerFromChunks(first_conv_chunk,
                                            second_conv_chunk));

  printf("Conv1: %d x %d occ x %d\n"
         "Conv2: %d x %d occ x %d\n",
         first_conv_chunk.num_occurrences_across,
         first_conv_chunk.num_occurrences_down,
         first_conv_chunk.num_features,
         second_conv_chunk.num_occurrences_across,
         second_conv_chunk.num_occurrences_down,
         second_conv_chunk.num_features);

  const int NUM_LAYER2_FEATURES = 32;

  int num_conv1 =
    CONV1_FEATURES * first_conv_chunk.num_occurrences_across *
    first_conv_chunk.num_occurrences_down;
  Chunk next_conv1 =
    Network::Make2DConvolutionChunk(
        0,
        // Each occurrence becomes CONV1_FEATURES wide.
        CONV1_FEATURES * first_conv_chunk.num_occurrences_across,
        first_conv_chunk.num_occurrences_down,
        // Process 2x2 blocks (of all features) into 32 features.
        NUM_LAYER2_FEATURES, CONV1_FEATURES * 2, 2,
        // no overlap
        CONV1_FEATURES * 2, 2,
        tf, WEIGHT_UPDATE);

  /*
  int num_conv2 =
    CONV2_FEATURES * second_conv_chunk.num_occurrences_across *
    second_conv_chunk.num_occurrences_down;
  */
  Chunk next_conv2 =
    Network::Make2DConvolutionChunk(
        // skip first conv chunk
        num_conv1,
        CONV2_FEATURES * second_conv_chunk.num_occurrences_across,
        second_conv_chunk.num_occurrences_down,
        // As above, into 32 features.
        NUM_LAYER2_FEATURES, CONV2_FEATURES * 2, 2,
        // No overlap.
        CONV2_FEATURES * 2, 2,
        tf, WEIGHT_UPDATE);

  layers.push_back(Network::LayerFromChunks(next_conv1,
                                            next_conv2));

  printf("Conv1: %d x %d occ x %d\n"
         "Conv2: %d x %d occ x %d\n",
         next_conv1.num_occurrences_across,
         next_conv1.num_occurrences_down,
         next_conv1.num_features,
         next_conv2.num_occurrences_across,
         next_conv2.num_occurrences_down,
         next_conv2.num_features);

  const int NUM_DEEP = 2;
  int layer_size = 1024;
  for (int i = 0; i < NUM_DEEP; i++) {
    int prev_size = layers.back().num_nodes;
    Chunk sparse =
      Network::MakeRandomSparseChunk(
          &rc, layer_size, {{.span_start = 0, .span_size = prev_size,
                             .ipn = prev_size / 16}},
          tf, WEIGHT_UPDATE);
    layers.push_back(Network::LayerFromChunks(sparse));
    layer_size >>= 1;
  }

  // Output layer. Uses IDENTITY so that any transfer function can
  // generate any gamut (but still be "linear").
  Chunk dense_out =
    Network::MakeDenseChunk(OUTPUT_SIZE,
                            0, layers.back().num_nodes,
                            IDENTITY, WEIGHT_UPDATE);

  layers.push_back(Network::LayerFromChunks(dense_out));

  CHECK(layers.back().num_nodes > 0);

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizationParams rparams;
  RandomizeNetwork(&rc, net.get(), rparams, 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}

int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 4) <<
    "./train-cifar10.exe dir transfer_function rounds\n"
    "Notes:\n"
    "  dir must exist. Resumes training if the dir\n"
    "    contains a model file.\n"
    "  transfer_function should be one of\n"
    "    SIGMOID, RELU, LEAKY_RELU, IDENTITY\n"
    "    TANH, GRAD1,\n";

  const string dir = argv[1];
  const TransferFunction tf = ParseTransferFunction(argv[2]);
  const int64 max_rounds = (int64)atoll(argv[3]);
  CHECK(max_rounds > 0) << argv[3];

  cl = new CL;

  const string model_file = Util::dirplus(dir, MODEL_NAME);

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_file));

  if (net.get() == nullptr) {
    net = NewImagesNetwork(tf);
    CHECK(net.get() != nullptr);
    net->SaveToFile(model_file);
    printf("Wrote to %s\n", model_file.c_str());
  }

  net->StructuralCheck();
  net->NaNCheck(model_file);

  TrainParams tparams = DefaultParams();

  if (net->rounds >= max_rounds) {
    printf("%s: Already trained %lld rounds.\n",
           model_file.c_str(), net->rounds);
  } else {
    Train(dir, net.get(), max_rounds, tparams);
  }

  printf("OK\n");
  return 0;
}

