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
#include "error-history.h"

#include "plugins.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

using int64 = int64_t;

#define MODEL_NAME "params.val"

// Size of time domain window in samples.
constexpr int WINDOW_SIZE = 256; // 1024;
using Plugin = Decimate<WINDOW_SIZE>;
constexpr int NUM_PARAMS = Plugin::NUM_PARAMETERS;

#define WORDS_BEFORE 3
#define WORDS_AFTER 2
#define NUM_WORDS (WORDS_BEFORE + WORDS_AFTER + 1)
#define WORDLIST_SIZE 1024

// Examples are 64k * num_words ~1.5MB each, although they are
// extremely sparse.
static constexpr int EXAMPLES_PER_ROUND = 1000;

#if 0
struct ExampleThread {

  static constexpr int TARGET_SIZE = 3;

  std::vector<int> GetExamples() {
    for (;;) {
      {
        MutexLock ml(&m);
        if (!q.empty()) {
          std::vector<int> ret = std::move(q.back());
          q.pop_back();
          return ret;
        }
      }

      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  ExampleThread() {
    work_thread.reset(new std::thread(&Generate, this));
  }

  ~ExampleThread() {
    LOG(FATAL) << "unimplemented";
  }

private:
  void Generate() {
    printf("Started example thread.\n");
    for (;;) {
      const bool need_example = [&](){
          MutexLock ml(&m);
          return q.size() < TARGET_SIZE;
        }();

      if (need_example) {
        // examples_per_round * num_words
        std::vector<int> example_words;

        // Fill 'em

        {
          MutexLock ml(&m);
          q.push_front(std::move(example_words));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
  }

  std::mutex m;
  std::deque<std::vector<int>> q;

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
  // This is conservative, but with larger values I would
  // get divergence after hundreds of thousands of rounds.
  static constexpr float ADAM_EPSILON = 1e-6;

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
  constexpr int HISTORY_EVERY_VERBOSE = 10;
  int64 total_verbose = 0;
  constexpr int TIMING_EVERY = 1000;

  // TODO: To utilities
  std::vector<std::unique_ptr<ImageRGBA>> images;
  std::vector<int> image_x;
  constexpr int IMAGE_WIDTH = 3000;
  constexpr int IMAGE_HEIGHT = 1000;
  constexpr int IMAGE_EVERY = 10;
  for (int i = 0; i < net->layers.size(); i++) {
    // XXX skip input layer?
    image_x.push_back(0);
    images.emplace_back(new ImageRGBA(IMAGE_WIDTH, IMAGE_HEIGHT));
    images.back()->Clear32(0x000000FF);
    images.back()->BlendText2x32(
        2, 2, 0x9999AAFF,
        StringPrintf("Layer %d | Start Round %lld | 1 px = %d rounds ",
                     i, net->rounds, IMAGE_EVERY));
  }

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

  // Used to fill the input and output for the training example,
  // and preserved on the CPU to compute loss in verbose rounds.
  constexpr int INPUT_SIZE = (WORDS_BEFORE + WORDS_AFTER) *
    WORDLIST_SIZE;
  constexpr int OUTPUT_SIZE = (WORDS_BEFORE + WORDS_AFTER + 1) *
    WORDLIST_SIZE;

  std::vector<float> inputs(INPUT_SIZE * EXAMPLES_PER_ROUND, 0.0f);
  std::vector<float> expecteds(OUTPUT_SIZE * EXAMPLES_PER_ROUND, 0.0f);

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
    {
      Timer frag_timer;

      // All examples for the round, flat, as word ids.
      std::vector<int> examples = example_thread.GetExamples();
      frag_ms += frag_timer.MS();

      Timer example_timer;
      for (float &f : inputs) f = 0.0f;
      for (float &f : expecteds) f = 0.0f;

      // PERF probably skip parallelism here?
      ParallelComp(
          EXAMPLES_PER_ROUND,
          [&](int i) {
            // start position in the word id array.
            int ex_start = NUM_WORDS * i;

            // encoded_words already put the target word at the end.
            // Input is WORDS_BEFORE WORDS_AFTER
            // Output is WORDS_BEFORE WORDS_AFTER target_word
            for (int w = 0;
                 w < (WORDS_BEFORE + WORDS_AFTER);
                 w++) {
              int hot = examples[ex_start + w];
              inputs[INPUT_SIZE * i + w * WORDLIST_SIZE + hot] = 1.0f;
            }

            for (int w = 0;
                 w < (WORDS_BEFORE + WORDS_AFTER + 1);
                 w++) {
              int hot = examples[ex_start + w];
              expecteds[OUTPUT_SIZE * i + w * WORDLIST_SIZE + hot] = 1.0f;
            }
          },
          max_parallelism);

      // PERF we could perhaps clear and set ones directly on GPU
      // faster than we can copy?
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
      ParallelComp(net->layers.size() - 1,
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
        ParallelMapi(expected,
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

      // XXX would be better if this was more accurate,
      // but we only want to read from GPU if we're going to
      // actually do anything below
      if (images.size() >= 2 &&
          images[1].get() != nullptr) {

        net_gpu->ReadFromGPU();

        for (int target_layer = 1; target_layer < net->layers.size();
             target_layer++) {
          ImageRGBA *image = images[target_layer].get();
          if (image == nullptr) continue;

          // If we exceeded the bounds, shrink in place.
          if (image_x[target_layer] >= image->Width()) {
            printf("Shrink image for layer %d\n", target_layer);
            // Skips over the text at the top (but not any pixels that
            // were drawn over it...)
            for (int y = 18; y < IMAGE_HEIGHT; y++) {
              static_assert(IMAGE_WIDTH % 2 == 0,
                            "Assumes even width of image");
              // First half gets the entire image shrunk 2:1.
              for (int x = 0; x < IMAGE_WIDTH / 2; x++) {
                const auto [r1, g1, b1, a1] = image->GetPixel(x * 2 + 0, y);
                const auto [r2, g2, b2, a2] = image->GetPixel(x * 2 + 1, y);
                // We know we have full alpha here.
                uint8 r = ((uint32)r1 + (uint32)r2) >> 1;
                uint8 g = ((uint32)g1 + (uint32)g2) >> 1;
                uint8 b = ((uint32)b1 + (uint32)b2) >> 1;
                image->SetPixel(x, y, r, g, b, 0xFF);
              }
              // And clear the second half.
              for (int x = IMAGE_WIDTH / 2; x < image->Width(); x++) {
                image->SetPixel(x, y, 0, 0, 0, 0xFF);
              }
            }
            image_x[target_layer] = IMAGE_WIDTH / 2;
          }

          const int ix = image_x[target_layer];
          if (ix >= image->Width()) continue;

          CHECK(net->layers.size() > 0);
          CHECK(target_layer < net->layers.size());
          const Layer &layer = net->layers[target_layer];
          CHECK(layer.chunks.size() > 0);
          // For this network the last chunk is most interesting,
          // so we can skip the copy chunks
          const Chunk &chunk = layer.chunks.back();
          auto ToScreenY = [](float w) {
              int yrev = w * float(IMAGE_HEIGHT / 4) + (IMAGE_HEIGHT / 2);
              int y = IMAGE_HEIGHT - yrev;
              // Always draw on-screen.
              return std::clamp(y, 0, IMAGE_HEIGHT - 1);
            };
          // 1, -1, x axis
          if (ix & 1) {
            image->BlendPixel32(ix, ToScreenY(1), 0xCCFFCC40);
            image->BlendPixel32(ix, ToScreenY(0), 0xCCCCFFFF);
            image->BlendPixel32(ix, ToScreenY(-1), 0xFFCCCC40);
          }

          uint8 weight_alpha =
            std::clamp((255.0f / sqrtf(chunk.weights.size())), 10.0f, 240.0f);

          for (float w : chunk.weights) {
            // maybe better to AA this?
            image->BlendPixel32(ix, ToScreenY(w),
                                0xFFFFFF00 | weight_alpha);
          }

          uint8 bias_alpha =
            std::clamp((255.0f / sqrtf(chunk.biases.size())), 10.0f, 240.0f);

          for (float b : chunk.biases) {
            image->BlendPixel32(ix, ToScreenY(b),
                                0xFF777700 | bias_alpha);
          }

          if (chunk.weight_update == ADAM) {
            CHECK(chunk.weights_aux.size() == 2 * chunk.weights.size());
            CHECK(chunk.biases_aux.size() == 2 * chunk.biases.size());
            for (int idx = 0; idx < chunk.weights.size(); idx++) {
              const float m = chunk.weights_aux[idx * 2 + 0];
              const float v = sqrtf(chunk.weights_aux[idx * 2 + 1]);

              image->BlendPixel32(ix, ToScreenY(m),
                                  0xFFFF0000 | weight_alpha);
              image->BlendPixel32(ix, ToScreenY(v),
                                  0xFF00FF00 | weight_alpha);
            }
            // Also bias aux?
          }


          if (ix % 100 == 0) {
            string filename = StringPrintf("train-image-%d.png",
                                           target_layer);
            image->Save(filename);
            printf("Wrote %s\n", filename.c_str());
          }
          image_x[target_layer]++;
        }
      }
      image_ms += image_timer.MS();
    }

    static constexpr double SAVE_EVERY_SEC = 120.0;
    bool save_timeout = false;
    if ((train_timer.MS() / 1000.0) > last_save + SAVE_EVERY_SEC) {
      save_timeout = true;
      last_save = train_timer.MS() / 1000.0;
    }

    if (SAVE_INTERMEDIATE && (save_timeout || finished ||
                              iter == 1000 || iter % 5000 == 0)) {
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
#endif

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
    int NUM_FEATURES = 0;
    int OCC_DIVISOR = 1;
    int FFT_WINDOW = 0;
    float FFT_DENSITY = 0.25f;
  };

  // For the initial convolution.
  // 44.1 would be one millisecond.
  static constexpr int CONV_WIDTH = 48;
  // 1D convolution with stride 1.
  // static constexpr int NUM_OCC = WINDOW_SIZE - CONV_WIDTH + 1;
  // Keep the same size, although this is not necessary.
  static constexpr int NUM_FEATURES = CONV_WIDTH;

  static constexpr int NGLOB_0 = 0;
  
  // Each one actually yields two layers in the steady state.
  vector<Structure> structures = {
    Structure{.G = 0, .NGLOB = 0,
              .NUM_FEATURES = 1, .OCC_DIVISOR = 1,
              .FFT_WINDOW = WINDOW_SIZE, .FFT_DENSITY = 1.0},
    Structure{.G = 8, .NGLOB = 56,
              .NUM_FEATURES = NUM_FEATURES, .OCC_DIVISOR = 2,
              .FFT_WINDOW = (int)(WINDOW_SIZE * 0.75), .FFT_DENSITY = 0.25},
    Structure{.G = 8, .NGLOB = 120,
              .NUM_FEATURES = (int)(NUM_FEATURES * 0.75), .OCC_DIVISOR = 2,
              .FFT_WINDOW = (int)(WINDOW_SIZE * 0.50), .FFT_DENSITY = 0.25},
    Structure{.G = 8, .NGLOB = 56,
              .NUM_FEATURES = (int)(NUM_FEATURES * 0.625), .OCC_DIVISOR = 2,
              .FFT_WINDOW = (int)(WINDOW_SIZE * 0.25), .FFT_DENSITY = 0.25},
  };

  for (const Structure &s : structures) {
    CHECK(s.G <= s.NGLOB);
    CHECK(s.G >= 0);
    CHECK(s.NGLOB >= 0);
  }
  
  std::vector<Layer> layers;  
  auto L = [&](const std::vector<Chunk> &chunks) {
      int num_nodes = 0;
      for (const Chunk &chunk : chunks) num_nodes += chunk.num_nodes;
      return Layer{.num_nodes = num_nodes, .chunks = chunks};
    };

  
  static constexpr int INPUT_SIZE = NGLOB_0 + WINDOW_SIZE * 2;
  static_assert(INPUT_SIZE > 0);
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  layers.push_back(L({input_chunk}));

  CHECK(layers.back().num_nodes > 0);
  
  int prev_occurrences = WINDOW_SIZE;  
  for (int s = 1; s < structures.size(); s++) {
    const Structure &prev = structures[s - 1];
    const Structure &next = structures[s];
    
    CHECK(next.G <= next.NGLOB);
    CHECK(prev_occurrences % next.OCC_DIVISOR == 0) <<
      next.OCC_DIVISOR << " must divide " << prev_occurrences;

    // mostly we are mapping from 'prev' to 'next', but we actually
    // create 'next' globals with the first of the two layers. 
    // XXX need to be clearer about prev vs next use of NGLOB, G, etc.
    
    // Add the two layers. The first one updates globals densely
    // and distributes the first G globals to the convolution occurrences.

    {
      CHECK(next.NGLOB > 0) << "Could maybe handle this but the chunk "
        "would be degenerate";
      Chunk glob;
      glob.type = CHUNK_DENSE;
      glob.transfer_function = LEAKY_RELU;
      glob.num_nodes = next.NGLOB;
      glob.span_start = 0;
      glob.span_size = layers.back().num_nodes;
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
      Chunk conv;
      conv.type = CHUNK_CONVOLUTION_ARRAY;
      conv.transfer_function = LEAKY_RELU;
      conv.num_features = next.NUM_FEATURES;
      conv.occurrence_x_stride = pattern_width;
      conv.occurrence_y_stride = 1;
      conv.pattern_width = pattern_width;
      conv.pattern_height = 1;
      conv.src_width = pattern_width * prev_occurrences / next.OCC_DIVISOR;
      conv.src_height = 1;
      // but we already introduced the next NGLOB above.
      conv.span_start = next.NGLOB;
      conv.span_size = conv.src_width;
      conv.indices_per_node = pattern_width;

      {
        const auto [indices, this_num_nodes,
                    num_occurrences_across, num_occurrences_down] =
          Network::MakeConvolutionArrayIndices(conv.span_start,
                                               conv.span_size,
                                               conv.num_features,
                                               conv.pattern_width,
                                               conv.pattern_height,
                                               conv.src_width,
                                               conv.src_height,
                                               conv.occurrence_x_stride,
                                               conv.occurrence_y_stride);
        CHECK(this_num_nodes ==
              next.NUM_FEATURES *
              prev_occurrences / next.OCC_DIVISOR);
        conv.num_nodes = this_num_nodes;
        conv.width = conv.num_nodes;
        conv.height = 1;
        conv.channels = 1;

        CHECK(num_occurrences_across == prev_occurrences / next.OCC_DIVISOR);
        conv.num_occurrences_across = num_occurrences_across;
        CHECK(num_occurrences_down == 1);
        conv.num_occurrences_down = num_occurrences_down;
        conv.indices = indices;

        conv.weights = std::vector<float>(
            conv.indices_per_node * conv.num_features,
            0.0f);
        conv.biases = std::vector<float>(conv.num_features, 0.0f);
      }

      conv.weight_update = ADAM;
      conv.weights_aux.resize(conv.weights.size() * 2, 0.0f);
      conv.biases_aux.resize(conv.biases.size() * 2, 0.0f);

      
      
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

  // Train(net.get());

  printf("OK\n");
  return 0;
}
