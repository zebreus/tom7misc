// (From direct-words; needs to be gutted and rewritten for pluginvert!)

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

#include <api/fftw3.h>

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
#include "error-history.h"
#include "audio-database.h"
#include "plugins.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

using int64 = int64_t;

static constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;

#define MODEL_NAME "params.val"

// Size of time domain window in samples.
constexpr int WINDOW_SIZE = 1024;
using Plugin = Convolve4<WINDOW_SIZE>;
constexpr int NUM_PARAMS = Plugin::NUM_PARAMETERS;

constexpr int INPUT_SIZE = WINDOW_SIZE + WINDOW_SIZE;
constexpr int OUTPUT_SIZE = NUM_PARAMS;
constexpr int SAMPLE_RATE = 44100;

// Examples are about 2k.
static constexpr int EXAMPLES_PER_ROUND = 1000;

struct ExampleThread {
  using example_type = std::pair<std::vector<float>, std::vector<float>>;

  static constexpr int P_MP3 = 100;
  static constexpr int P_SIN = 12;
  static constexpr int P_SQUARE = 12;

  // Rest is white noise.
  static_assert(P_MP3 + P_SIN + P_SQUARE <= 256);

  static constexpr int TARGET_SIZE = 3;

  example_type GetExamples() {
    for (;;) {
      {
        MutexLock ml(&m);
        if (!q.empty()) {
          example_type ret = std::move(q.back());
          q.pop_back();
          return ret;
        }
      }

      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  ExampleThread() {
    audio_database.reset(new AudioDatabase(WINDOW_SIZE, "corpus"));
    work_thread.reset(new std::thread(&Generate, this, 1));
  }

  ~ExampleThread() {
    LOG(FATAL) << "unimplemented";
  }

private:
  static vector<float> RandomSamples(AudioDatabase *db, ArcFour *rc) {
    uint8 method = rc->Byte();
    if (method < P_MP3) {
      return db->GetBuffer();
    }
    method -= P_MP3;

    if (method < P_SIN) {
      vector<float> ret;
      ret.reserve(WINDOW_SIZE);

      float amp = RandFloat(rc);
      // PERF can bake in SAMPLE_RATE divisor too
      float freq_twopi =
        TWO_PI * (10.0f + RandFloat(rc) * 20000.0f);
      // TODO: sample phases uniformly
      const int phase = RandTo(rc, SAMPLE_RATE);
      for (int i = 0; i < WINDOW_SIZE; i++) {
        float sec = (i + phase) / (float)SAMPLE_RATE;
        ret.push_back(amp * sin(freq_twopi * sec));
      }
      return ret;
    }
    method -= P_SIN;

    if (method < P_SQUARE) {
      // Simple square wave, not band-limited
      vector<float> ret;
      ret.reserve(WINDOW_SIZE);

      float amp = RandFloat(rc);
      float width = 2.0f + (RandFloat(rc) * (WINDOW_SIZE - 2.0f));
      float count = RandFloat(rc) * WINDOW_SIZE;
      bool on = rc->Byte() & 1;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        count -= 1.0f;
        if (count < 0.0f) {
          on = !on;
          count += width;
        }
        ret.push_back(on ? amp : -amp);
      }
      return ret;
    }
    method -= P_SQUARE;

    // Otherwise, white noise.
    {
      vector<float> ret;
      ret.reserve(WINDOW_SIZE);

      for (int i = 0; i < WINDOW_SIZE; i++) {
        float f = RandFloat(rc);
        ret.push_back(f * 2.0f - 1.0f);
      }
      return ret;
    }
  }

  // Perhaps should have more than one generation thread...
  void Generate(int id) {
    printf("Started example thread %d.\n", id);
    ArcFour rc(StringPrintf("examples.%lld.%d\n", time(nullptr), id));

    // TODO: float ffts
    using element_type = double;

    fftw_plan plan;
    [[maybe_unused]] fftw_plan rplan;
    element_type *in = (element_type*)
      fftw_malloc(sizeof (element_type) * WINDOW_SIZE);
    element_type *out = (element_type*)
      fftw_malloc(sizeof (element_type) * WINDOW_SIZE);

    // Discrete Hartley Transform is its own inverse.
    static constexpr fftw_r2r_kind fwd = FFTW_DHT;
    static constexpr fftw_r2r_kind inv = FFTW_DHT;

    plan = fftw_plan_r2r_1d(WINDOW_SIZE, in, out,
                            fwd, FFTW_MEASURE);
    // printf("Forward plan:\n");
    // fftw_print_plan(plan);

    rplan = fftw_plan_r2r_1d(WINDOW_SIZE, in, out,
                             inv, FFTW_MEASURE);

    // printf("Inverse plan:\n");
    // fftw_print_plan(rplan);

    for (;;) {
      const bool need_example = [&](){
          MutexLock ml(&m);
          return q.size() < TARGET_SIZE;
        }();

      if (need_example) {
        // examples_per_round * INPUT_SIZE;
        std::vector<float> examples;
        // examples_per_round * NUM_PARAMS
        std::vector<float> outputs;

        // Fill 'em
        for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
          // Random parameters
          std::array<float, Plugin::NUM_PARAMETERS> params;
          for (int p = 0; p < Plugin::NUM_PARAMETERS; p++) {
            const Param &param = Plugin::PARAMS[p];
            float f = RandomBeta(&rc, param.beta_a, param.beta_b);
            // Note!
            // We train to predict the position between [lb, ub]
            // (as a number in [0, 1]) because the network has some
            // built in priors against large values (e.g. absolute
            // limits on weights/biases when clipping is turned on)
            // and to avoid accuracy problems with large floats.
            outputs.push_back(f);

            params[p] = param.lb + (param.ub - param.lb) * f;
          }

          vector<float> samples =
            Plugin::Process(RandomSamples(audio_database.get(), &rc),
                            params);
          CHECK(samples.size() == WINDOW_SIZE);
          for (int s = 0; s < WINDOW_SIZE; s++)
            in[s] = samples[s];
          fftw_execute(plan);

          // Regular samples
          for (float f : samples) examples.push_back(f);
          // 'FFT' values
          for (int i = 0; i < WINDOW_SIZE; i++) {
            // FFTW output needs to be normalized
            examples.push_back((float)out[i] * (1.0f / WINDOW_SIZE));
          }
        }

        CHECK(examples.size() == EXAMPLES_PER_ROUND * INPUT_SIZE);
        CHECK(outputs.size() == EXAMPLES_PER_ROUND * OUTPUT_SIZE);

        {
          MutexLock ml(&m);
          q.push_front(
              make_pair(std::move(examples), std::move(outputs)));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }

    /*
    fftw_free(in);
    fftw_free(out);
    fftw_destroy_plan(plan);
    fftw_destroy_plan(rplan);
    */
  }

  std::unique_ptr<AudioDatabase> audio_database;

  std::mutex m;
  std::deque<example_type> q;

  std::unique_ptr<std::thread> work_thread;
};

static void Train(Network *net) {
  ExampleThread example_thread;

  ErrorHistory error_history("error-history.tsv");

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;
  // XXX need to reduce this over time
  static constexpr float LEARNING_RATE = 0.01f;
  // This is conservative, but with larger exponents I would
  // get divergence after hundreds of thousands of rounds.
  // This happened again with the plugin parameter predictor
  // with a value of 1e-6!
  static constexpr float ADAM_EPSILON = 1e-4;

  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  static constexpr float DECAY_RATE = 0.999999f;
  static constexpr bool DO_DECAY = false;

  // On a verbose round we compute training error and print out
  // examples.
  constexpr int VERBOSE_EVERY = 100;
  // We save this to the error history file every this many
  // verbose rounds.
  constexpr int HISTORY_EVERY_VERBOSE = 5;
  int64 total_verbose = 0;
  constexpr int TIMING_EVERY = 500;

  constexpr int IMAGE_EVERY = 100;
  TrainingImages images(*net, "train", MODEL_NAME, IMAGE_EVERY);

  printf("Training!\n");

  auto net_gpu = make_unique<NetworkGPU>(cl, net);

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, *net);
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, *net);
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, *net);
  [[maybe_unused]]
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, *net, DECAY_RATE);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, *net, EXAMPLES_PER_ROUND,
                                      ADAM_EPSILON);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));

  CHECK(!net->layers.empty());
  CHECK(net->layers[0].num_nodes == INPUT_SIZE);
  CHECK(net->layers.back().num_nodes == OUTPUT_SIZE);

  double round_ms = 0.0;
  double frag_ms = 0.0;
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
  for (int iter = 0; true; iter++) {
    Timer round_timer;

    const bool verbose_round = (iter % VERBOSE_EVERY) == 0;

    // Initialize training examples.
    // (PERF: parallelize?)

    // TODO: Rename this timer. It's like, stalling on getexamples.
    Timer frag_timer;
    // All examples for the round, flat, as word ids.
    auto [inputs, expecteds] = example_thread.GetExamples();
    frag_ms += frag_timer.MS();

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
        forward_cl->RunForward(net_gpu.get(), training.get(), src_layer);
      }
      forward_ms += forward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    {
      Timer error_timer;
      error_cl->SetOutputError(net_gpu.get(), training.get());
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
        backward_cl->BackwardLayer(net_gpu.get(), training.get(), dst_layer);
      }
      backward_ms += backward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    if (DO_DECAY) {
      Timer decay_timer;
      for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
        decay_cl->Decay(net_gpu.get(), layer_idx);
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
                       update_cl->Update(net_gpu.get(), training.get(),
                                         LEARNING_RATE, layer_idx);
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

      string example_correct, example_predicted;
      std::vector<float> losses =
        UnParallelMapi(expected,
                       [&](int idx, const std::vector<float> &exp) {
                         std::vector<float> got;
                         got.resize(exp.size());
                         CHECK(got.size() == OUTPUT_SIZE);
                         CHECK(idx >= 0 && idx < EXAMPLES_PER_ROUND);
                         training->ExportOutput(idx, &got);

                         // XXX if this is all we're doing, we should
                         // instead use the already-computed
                         // values from GetOutputError, perhaps
                         // summing on GPU
                         float loss = 0.0f;
                         for (int i = 0; i < exp.size(); i++) {
                           loss += fabsf(exp[i] - got[i]);
                         }

                         return loss;
                       }, max_parallelism);

      if (VERBOSE > 1)
        printf("Got losses.\n");

      float min_loss = 1.0f / 0.0f, average_loss = 0.0f, max_loss = 0.0f;
      for (auto loss_dist : losses) {
        min_loss = std::min(loss_dist, min_loss);
        max_loss = std::max(loss_dist, max_loss);
        average_loss += loss_dist;
      }
      average_loss /= losses.size();

      const double total_sec = train_timer.MS() / 1000.0;
      const double eps = total_examples / total_sec;

      printf("%d: %.3f<%.3f<%.3f", iter,
             min_loss, average_loss, max_loss);
      printf(" (%.2f eps)\n", eps);

      if ((total_verbose % HISTORY_EVERY_VERBOSE) == 0) {
        error_history.Add(net->rounds, average_loss, false);
      }
      total_verbose++;
      loss_ms += loss_timer.MS();
    }

    if ((iter % IMAGE_EVERY) == 0) {
      Timer image_timer;
      net_gpu->ReadFromGPU();
      images.Sample(*net);
      image_ms += image_timer.MS();
    }

    static constexpr double SAVE_EVERY_SEC = 180.0;
    bool save_timeout = false;
    if ((train_timer.MS() / 1000.0) > last_save + SAVE_EVERY_SEC) {
      save_timeout = true;
      last_save = train_timer.MS() / 1000.0;
    }

    if (SAVE_INTERMEDIATE && (save_timeout || finished ||
                              iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      const string file = MODEL_NAME;
      net->SaveToFile(file);
      if (VERBOSE)
        printf("Wrote %s\n", file.c_str());
      error_history.Save();
    }

    // Parameter for average_loss termination?
    if (finished) {
      printf("Successfully trained!\n");
      return;
    }

    round_ms += round_timer.MS();

    if (iter % TIMING_EVERY == 0) {
      double accounted_ms = frag_ms + example_ms + forward_ms +
        error_ms + decay_ms + backward_ms + update_ms + loss_ms +
        image_ms;
      double other_ms = round_ms - accounted_ms;
      double pct = 100.0 / round_ms;
      printf("%.1f%% f  "
             "%.1f%% ex  "
             "%.1f%% fwd  "
             "%.1f%% err  "
             "%.1f%% dec  "
             "%.1f%% bwd  "
             "%.1f%% up  "
             "%.1f%% loss "
             "%.1f%% img "
             "%.1f%% other\n",
             frag_ms * pct,
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
      printf("%.1fms f  "
             "%.1fms ex  "
             "%.1fms fwd  "
             "%.1fms err  "
             "%.1fms dec  "
             "%.1fms bwd  "
             "%.1fms up  "
             "%.2fms loss  "
             "%.2fms img  "
             "%.1fms other\n",
             frag_ms * msr,
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
}

static unique_ptr<Network> NewParamsNetwork() {
  // Deterministic!
  ArcFour rc("learn-params-network");

  // After the input layer we have a repeated structure here.
  // Every layer looks like this:
  //  [G][lobals][ ][ ]  conv occs     [ ][ ][ --  fft  -- ]
  //  |-NGLOB---||--NUM_FEATURES * NUM_OCC--||-FFT_WINDOW--|
  //
  // Where for the first layer, NUM_FEATURES is just 1 (samples,
  // although we could easily support stereo pairs!) and NUM_OCC is
  // WINDOW_SIZE, as though a 1x1 convolution with stride 1 was applied.
  // FFT_WINDOW is WINDOW_SIZE (DHT) and G=NGLOB=0, as there are
  // no globals yet.
  //
  // (Aside: for regularity and because it might be useful, we could
  // consider having globals in the inputs. For predicting the
  // waveform, these would include the parameter values. Other
  // stuff might include sample rate (although this will be
  // a constant during training?), min, max, average samples,
  // or "mipmaps" of the waveform itself.)

  struct Structure {
    int G = 0;
    int NGLOB = 0;
    float GLOB_DENSITY = 0.25f;
    int NUM_FEATURES = 0;
    int OCC_DIVISOR = 1;
    int FFT_WINDOW = 0;
    float FFT_DENSITY = 0.25f;
  };

  // For the initial convolution.
  // 44.1 would be one millisecond.

  static constexpr int INITIAL_CONV_WIDTH = 45;
  static constexpr int DIV1 = 2, DIV2 = 2, DIV3 = 5;

  {
    static constexpr int INITIAL_OCC = WINDOW_SIZE - INITIAL_CONV_WIDTH + 1;
    static_assert((INITIAL_OCC % (DIV1 * DIV2 * DIV3)) == 0);
  }

  // Each one actually yields two layers in the steady state.
  vector<Structure> structures = {
    // Describing the output of the first convolution; not all of
    // these parameters are used.
    Structure{
      .G = 0, .NGLOB = 0, .GLOB_DENSITY = 1.0,
      .NUM_FEATURES = INITIAL_CONV_WIDTH, .OCC_DIVISOR = 1,
      .FFT_WINDOW = WINDOW_SIZE, .FFT_DENSITY = 1.0},
    Structure{
      .G = 8, .NGLOB = 56, .GLOB_DENSITY = 0.125f,
      .NUM_FEATURES = (int)(INITIAL_CONV_WIDTH * 0.75), .OCC_DIVISOR = DIV1,
      .FFT_WINDOW = (int)(WINDOW_SIZE * 0.75), .FFT_DENSITY = 0.25},
    Structure{
      .G = 8, .NGLOB = 120, .GLOB_DENSITY = 0.125f,
      .NUM_FEATURES = (int)(INITIAL_CONV_WIDTH * 0.625), .OCC_DIVISOR = DIV2,
      .FFT_WINDOW = (int)(WINDOW_SIZE * 0.50), .FFT_DENSITY = 0.25},
    Structure{
      .G = 8, .NGLOB = 56, .GLOB_DENSITY = 0.125f,
      .NUM_FEATURES = (int)(INITIAL_CONV_WIDTH * 0.5), .OCC_DIVISOR = DIV3,
      .FFT_WINDOW = (int)(WINDOW_SIZE * 0.25), .FFT_DENSITY = 0.25},
  };

  for (const Structure &s : structures) {
    CHECK(s.G <= s.NGLOB);
    CHECK(s.G >= 0);
    CHECK(s.NGLOB >= 0);
  }

  std::vector<Layer> layers;
  // XXX just call directly
  auto L = [](const std::vector<Chunk> &chunks) {
      return Network::LayerFromChunks(chunks);
    };


  static constexpr int INPUT_SIZE = WINDOW_SIZE * 2;
  static_assert(INPUT_SIZE > 0);
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  layers.push_back(L({input_chunk}));

  // First, convolution on the samples with stride=1.
  Chunk first_conv_chunk =
    Network::Make1DConvolutionChunk(
        // Entire window
        0, WINDOW_SIZE,
        // num features = conv width, but this is not necessary
        INITIAL_CONV_WIDTH, INITIAL_CONV_WIDTH,
        // full overlap so that we are not baking in any particular
        // phase.
        1,
        LEAKY_RELU, ADAM);
  Chunk copy_fft_chunk = Network::MakeCopyChunk(WINDOW_SIZE, WINDOW_SIZE);

  layers.push_back(L({first_conv_chunk, copy_fft_chunk}));

  // TODO: Maybe more initial convolution layers here?

  CHECK(layers.back().num_nodes > 0);

  int prev_occurrences = first_conv_chunk.num_occurrences_across;
  CHECK(prev_occurrences > 0);
  for (int s = 1; s < structures.size(); s++) {
    printf("s=%d, prev_occurrences=%d\n", s, prev_occurrences);
    const Structure &prev = structures[s - 1];
    const Structure &next = structures[s];

    CHECK(next.G <= next.NGLOB);
    CHECK(prev_occurrences % next.OCC_DIVISOR == 0) <<
      "On s=" << s << ", " <<
      next.OCC_DIVISOR << " must divide " << prev_occurrences;

    // mostly we are mapping from 'prev' to 'next', but we actually
    // create 'next' globals with the first of the two layers.
    // XXX need to be clearer about prev vs next use of NGLOB, G, etc.

    // Add the two layers. The first one updates globals using the
    // whole previous layer, and distributes the first G globals to
    // the convolution occurrences.

    {
      CHECK(next.NGLOB > 0) << "Could maybe handle this but the chunk "
        "would be degenerate";

      vector<Network::SparseSpan> spans;
      // Depend on entire global block, if there is any.
      if (prev.NGLOB > 0) {
        spans.push_back(
            Network::SparseSpan{
              .span_start = 0,
              .span_size = prev.NGLOB,
              .ipn = prev.NGLOB,
            });
      }

      // And depend sparsely on random subset of the rest.
      const int num_rest = layers.back().num_nodes - prev.NGLOB;
      CHECK(num_rest > 0);
      const int rest_ipn = std::max((int)(num_rest * next.GLOB_DENSITY), 1);
      spans.push_back(
          Network::SparseSpan{
            .span_start = prev.NGLOB,
            .span_size = num_rest,
            .ipn = rest_ipn});

      Chunk glob = Network::MakeRandomSparseChunk(
          &rc, next.NGLOB, spans, LEAKY_RELU, ADAM);

      // Then, distribute G (from the previous layer; might be none)
      // into each occurrence (after dividing), as well as copying the FFT.
      Chunk dist;
      dist.type = CHUNK_SPARSE;
      dist.transfer_function = IDENTITY;
      dist.span_start = 0;
      // entire window, since we use the globals and the fft too
      dist.span_size = layers.back().num_nodes;
      // We only need one copy of G when we combine multiple occurrences
      // because OCC_DIVISOR > 1.
      dist.num_nodes = (prev.G + prev.NUM_FEATURES * next.OCC_DIVISOR) *
        (prev_occurrences / next.OCC_DIVISOR) +
        prev.FFT_WINDOW;
      dist.indices_per_node = 1;
      for (int occ = 0; occ < prev_occurrences / next.OCC_DIVISOR; occ++) {
        const int feature_start =
          prev.NGLOB + occ * prev.NUM_FEATURES * next.OCC_DIVISOR;
        // First G globals.
        for (int i = 0; i < prev.G; i++)
          dist.indices.push_back(i);
        // Then the features.
        for (int f = 0; f < prev.NUM_FEATURES * next.OCC_DIVISOR; f++)
          dist.indices.push_back(feature_start + f);
      }
      const int fft_start = prev.NGLOB + prev_occurrences * prev.NUM_FEATURES;
      for (int i = 0; i < prev.FFT_WINDOW; i++)
        dist.indices.push_back(fft_start + i);
      CHECK(dist.indices.size() == dist.num_nodes);
      // Pure copy.
      dist.weights = std::vector<float>(dist.indices.size(), 1.0f);
      dist.biases = std::vector<float>(dist.num_nodes, 0.0f);
      dist.fixed = true;
      dist.weight_update = SGD;
      dist.width = dist.num_nodes;
      dist.height = 1;
      dist.channels = 1;

      layers.push_back(L({std::move(glob), std::move(dist)}));
    }

    // Now, the actual work.
    {
      // Dense globals to globals.
      // PERF: Maybe sparse would be better here too.
      // We already created next.NGLOB globals above, so we use next
      // here, not prev.
      Chunk glob;
      glob.type = CHUNK_DENSE;
      glob.transfer_function = LEAKY_RELU;
      glob.num_nodes = next.NGLOB;
      glob.span_start = 0;
      glob.span_size = next.NGLOB;
      glob.indices_per_node = glob.span_size;
      glob.weights.resize(glob.indices_per_node * glob.num_nodes);
      glob.biases.resize(glob.num_nodes);
      glob.weight_update = ADAM;
      glob.weights_aux.resize(glob.weights.size() * 2, 0.0f);
      glob.biases_aux.resize(glob.biases.size() * 2, 0.0f);
      glob.fixed = false;
      glob.width = glob.num_nodes;
      glob.height = 1;
      glob.channels = 1;

      // Convolution, including the G globals we distributed above.
      const int pattern_width =
        prev.G + prev.NUM_FEATURES * next.OCC_DIVISOR;
      Chunk conv =
        Network::Make1DConvolutionChunk(
            // Span is just the previous convolution part.
            next.NGLOB, pattern_width * prev_occurrences / next.OCC_DIVISOR,
            next.NUM_FEATURES, pattern_width,
            // No overlap
            pattern_width,
            LEAKY_RELU, ADAM);
      CHECK(conv.num_nodes ==
            next.NUM_FEATURES *
            prev_occurrences / next.OCC_DIVISOR);
      CHECK(conv.num_occurrences_across == prev_occurrences / next.OCC_DIVISOR);

      const int prev_fft_start =
        next.NGLOB + pattern_width * prev_occurrences / next.OCC_DIVISOR;

      const int index_pool_size = next.NGLOB + prev.FFT_WINDOW;
      const int sparse_ipn = std::max(
          (int)(next.FFT_DENSITY * index_pool_size), 1);
      // Finally, sparse FFT.
      Chunk fft;
      fft.type = CHUNK_SPARSE;
      fft.fixed = false;

      fft.transfer_function = LEAKY_RELU;
      fft.num_nodes = next.FFT_WINDOW;
      fft.span_start = 0;
      // Doesn't depend on conv part.
      fft.span_size = layers.back().num_nodes;
      fft.indices_per_node = sparse_ipn;
      fft.weight_update = ADAM;
      fft.width = fft.num_nodes;
      fft.height = 1;
      fft.channels = 1;

      fft.weights.resize(fft.num_nodes * sparse_ipn, 0.0f);
      fft.biases.resize(fft.num_nodes, 0.0f);
      fft.weights_aux.resize(fft.weights.size() * 2, 0.0f);
      fft.biases_aux.resize(fft.biases.size() * 2, 0.0f);

      // TODO: Always include the corresponding node. Prefer
      // nearby nodes.
      for (int n = 0; n < fft.num_nodes; n++) {
        // Add random indices.
        vector<uint32_t> index_pool;
        for (int i = 0; i < next.NGLOB; i++)
          index_pool.push_back(i);
        for (int i = 0; i < prev.FFT_WINDOW; i++)
          index_pool.push_back(prev_fft_start + i);
        CHECK(index_pool.size() == index_pool_size);
        Shuffle(&rc, &index_pool);
        index_pool.resize(sparse_ipn);
        std::sort(index_pool.begin(), index_pool.end());
        for (uint32_t idx : index_pool)
          fft.indices.push_back(idx);
      }


      prev_occurrences = conv.num_occurrences_across;
      layers.push_back(L({std::move(glob),
                          std::move(conv),
                          std::move(fft)}));
    }
  }

  // Finally, a dense layer to produce the required number of predicted
  // parameters.
  Chunk sink;
  sink.type = CHUNK_DENSE;
  sink.transfer_function = LEAKY_RELU;
  sink.num_nodes = NUM_PARAMS;
  sink.span_start = 0;
  sink.span_size = layers.back().num_nodes;
  sink.indices_per_node = sink.span_size;
  sink.weights.resize(sink.indices_per_node * sink.num_nodes);
  sink.biases.resize(sink.num_nodes);
  sink.weight_update = ADAM;
  sink.weights_aux.resize(sink.weights.size() * 2, 0.0f);
  sink.biases_aux.resize(sink.biases.size() * 2, 0.0f);
  sink.fixed = false;
  sink.width = sink.num_nodes;
  sink.height = 1;
  sink.channels = 1;

  layers.push_back(L({std::move(sink)}));

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizeNetwork(&rc, net.get(), 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}


int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  if (net.get() == nullptr) {
    net = NewParamsNetwork();
    CHECK(net.get() != nullptr);
    net->SaveToFile(MODEL_NAME);
    printf("Wrote to %s\n", MODEL_NAME);
  }

  net->StructuralCheck();
  net->NaNCheck(MODEL_NAME);

  Train(net.get());

  printf("OK\n");
  return 0;
}
