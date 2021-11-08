// Learns a "lexical" embedding of words into a 64-dimensional vector,
// by autoencoding. This was formerly called learn-words, but learn-words
// is a better name for the next phase, which learns an embedding of words
// that is more semantic (starting with this encoding, which is more
// efficient than one based on letters).

#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>

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
#include "wikipedia.h"
#include "error-history.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

using int64 = int64_t;

constexpr int MAX_WORD_LEN = 18;
constexpr int RADIX = 27;

static bool AllLetters(const string &s) {
  for (int i = 0; i < s.size(); i++) {
    if (s[i] < 'a' || s[i] > 'z') return false;
  }
  return true;
}

struct Wikibits {
  static constexpr int NUM_SHARDS = 128;

  Wikibits() : rc("wikibits" + StringPrintf("%lld", time(nullptr))) {
    std::vector<string> filenames;
    for (int i = 0; i < NUM_SHARDS; i++)
      filenames.push_back(StringPrintf("wikibits/wiki-%d.txt", i));
    std::vector<std::unordered_map<string, int64>> countvec =
      ParallelMap(filenames,
                  [](const std::string &filename) {
                    printf("Reading %s...\n", filename.c_str());
                    std::unordered_map<string, int64> counts;
                    int64 not_letters = 0;
                    auto co = Util::ReadFileOpt(filename);
                    CHECK(co.has_value());
                    string contents = std::move(co.value());
                    int64 pos = 0;
                    auto ReadByte = [&filename, &contents, &pos]() ->
                      uint8_t {
                        CHECK (pos < contents.size())
                          << filename << " @ " << pos << " of "
                          << contents.size();
                        return (uint8_t)contents[pos++];
                      };

                    auto Read32 = [&ReadByte]() {
                        uint32_t a = ReadByte();
                        uint32_t b = ReadByte();
                        uint32_t c = ReadByte();
                        uint32_t d = ReadByte();
                        return (a << 24) | (b << 16) | (c << 8) | d;
                      };

                    auto ReadString = [&contents, &pos, &ReadByte](int len) {
                        // printf("Read string of length %d:\n", len);
                        string s;
                        s.reserve(len);
                        for (int i = 0; i < len; i++) {
                          CHECK(pos < contents.size())
                            << i << "/" << len;
                          char c = ReadByte();
                          // printf("[%c]", c);
                          s.push_back(c);
                        }
                        return s;
                      };

                    int64 num_articles = 0;
                    auto Write = [&](uint32_t x) {
                      return StringPrintf("(%lld) %d [%c%c%c%c]",
                                          num_articles,
                                          x,
                                          (x >> 24) & 0xFF,
                                          (x >> 16) & 0xFF,
                                          (x >>  8) & 0xFF,
                                          x         & 0xFF);
                      };

                    while (pos < contents.size()) {
                      Wikipedia::Article art;
                      uint32_t t_len = Read32();
                      CHECK(t_len < 1000) << Write(t_len);
                      art.title = ReadString(t_len);
                      uint32_t b_len = Read32();
                      CHECK(b_len < 10000000) << Write(b_len);
                      art.body = ReadString(b_len);

                      for (int i = 0; i < art.title.size(); i++) {
                        CHECK(art.title[i] != 0);
                      }

                      for (int i = 0; i < art.body.size(); i++) {
                        CHECK(art.body[i] != 0);
                      }

                      // but convert article to words.
                      std::vector<string> tokens =
                        Util::Tokens(art.body,
                                     [](char c) { return isspace(c); });
                      for (const string &token : tokens) {
                        const string ltoken = Util::lcase(token);
                        if (AllLetters(ltoken)) {
                          counts[ltoken]++;
                        } else {
                          not_letters++;
                        }
                      }
                      num_articles++;
                    }

                    printf("Distinct words: %lld, Not letters: %lld\n",
                           counts.size(), not_letters);
                    return counts;
                  }, 8);

    printf("Now build all map:\n");
    for (const auto &m : countvec) {
      for (const auto &[s, c] : m) {
        counts[s] += c;
      }
    }

    printf("Done. All distinct words: %lld\n", counts.size());
    cdf.reserve(counts.size());
    for (const auto &[s, c] : counts) {
      cdf.emplace_back(s, c);
    }

    // Sort by descending frequency.
    // Alphabetical to break ties is arbitrary, but better to
    // have this be deterministic.
    std::sort(cdf.begin(), cdf.end(),
              [](const std::pair<string, int64> &a,
                 const std::pair<string, int64> &b) {
                if (a.second == b.second) {
                  return a.first < b.first;
                } else {
                  return a.second > b.second;
                }
              });

    // Count total mass, and replace count with count so far.
    total_mass = 0;
    for (auto &[s, c] : cdf) {
      int64 old_count = c;
      c = total_mass;
      total_mass += old_count;
    }

    // Sanity check.
    for (int64 i = 0; i < cdf.size(); i++) {
      if (i > 0) {
        CHECK(cdf[i].second > cdf[i - 1].second);
      }
      CHECK(cdf[i].second >= 0);
      CHECK(cdf[i].second < total_mass);
    }

    for (int64 i = 0; i < 5 && i < cdf.size(); i++) {
      const auto &[s, c] = cdf[i];
      printf("%lld: %s\n", c, s.c_str());
    }
    printf("...\n");
    for (int64 i = std::max((int64)0, (int64)cdf.size() - 5);
         i < cdf.size(); i++) {
      const auto &[s, c] = cdf[i];
      printf("%lld: %s\n", c, s.c_str());
    }

    {
      printf("0: %s\n", SampleAt(0).c_str());
      if (cdf.size() > 5) {
        int64 p = cdf[cdf.size() - 5].second;
        printf("%lld: %s\n", p, SampleAt(p).c_str());
      }
    }

    printf("CDF ready. Total mass: %lld\n", total_mass);
  }

  // XXX not thread-safe
  // Samples among all distinct words with the same probability.
  const string &RandomDistinctWord() {
    int64 pos = RandTo(&rc, cdf.size());
    return cdf[pos].first;
  }

  // XXX not thread safe
  // Samples according to observed frequency.
  const string &RandomWord() {
    int64 pos = RandTo(&rc, total_mass);
    return SampleAt(pos);
  }

private:
  // For above. Must be in range.
  const string &SampleAt(int64 pos) const {
    auto it = std::lower_bound(cdf.begin(), cdf.end(),
                               pos,
                               [](const std::pair<string, int64> &elt,
                                  int64 v) {
                                 return elt.second < v;
                               });
    CHECK(it != cdf.end()) << "Bug! pos " << pos << " total " << total_mass;
    return it->first;
  }

  std::unordered_map<string, int64> counts;
  // Words in arbitrary order, but with the cumulative frequency
  // so far (of all words before this one).
  std::vector<std::pair<string, int64>> cdf;
  int64 total_mass = 0LL;
  ArcFour rc;
};


static void Train(Network *net) {

  Wikibits wikibits;

  ErrorHistory error_history("learn-lex-error-history.tsv");

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;
  // Very small examples; could easily do 100x this...
  static constexpr int EXAMPLES_PER_ROUND = 1000;
  // XXX need to reduce this over time
  // static constexpr float LEARNING_RATE = 0.001f;
  static constexpr float LEARNING_RATE = 0.000125f;

  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  static constexpr float DECAY_RATE = 0.99999f;

  // On a verbose round we compute training error and print out
  // examples.
  constexpr int VERBOSE_EVERY = 100;
  // We save this to the error history file every this many
  // verbose rounds.
  constexpr int HISTORY_EVERY_VERBOSE = 10;
  int64 total_verbose = 0;
  constexpr int TIMING_EVERY = 1000;

  std::vector<std::unique_ptr<ImageRGBA>> images;
  constexpr int IMAGE_WIDTH = 3000;
  constexpr int IMAGE_HEIGHT = 1000;
  constexpr int IMAGE_EVERY = 1000;
  int image_x = 0;
  for (int i = 0; i < net->layers.size(); i++) {
    // XXX skip input layer?
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
    std::make_unique<UpdateWeightsCL>(EXAMPLES_PER_ROUND, cl, *net);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));

  // Used to fill the input and output for the training example,
  // and preserved on the CPU to compute loss in verbose rounds.
  constexpr int INPUT_SIZE = MAX_WORD_LEN * RADIX;
  std::vector<float> inputs(INPUT_SIZE * EXAMPLES_PER_ROUND, 0.0f);

  double round_ms = 0.0;
  double example_ms = 0.0;
  double forward_ms = 0.0;
  double error_ms = 0.0;
  double decay_ms = 0.0;
  double backward_ms = 0.0;
  double update_ms = 0.0;

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
      Timer example_timer;
      for (float &f : inputs) f = 0.0f;

      for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
        // One-hot
        string word = wikibits.RandomWord();
        for (int j = 0; j < MAX_WORD_LEN; j++) {
          const int c = j < word.size() ? word[j] - 'a' + 1 : 0;
          inputs[INPUT_SIZE * i + j * RADIX + c] = 1.0f;
        }
      }
      training->LoadInputs(inputs);
      training->LoadExpecteds(inputs);

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

    {
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
      printf("Updated errors.\n");

    total_examples += EXAMPLES_PER_ROUND;

    net->examples += EXAMPLES_PER_ROUND;
    net->rounds++;

    // (currently no way to actually finish, but we could set a
    // training error threshold below.)
    const bool finished = false;

    if (verbose_round) {
      // Get loss as abs distance, plus number of incorrect (as booleans).

      // PERF could do this on the flat vector, but we only need to
      // run it for verbose rounds
      std::vector<std::vector<float>> expected;
      expected.reserve(EXAMPLES_PER_ROUND);
      for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
        std::vector<float> one;
        one.resize(INPUT_SIZE, 0.0f);
        for (int j = 0; j < INPUT_SIZE; j++) {
          one[j] = inputs[i * INPUT_SIZE + j];
        }
        expected.emplace_back(std::move(one));
      }

      string example_correct, example_predicted;
      std::vector<std::pair<float, int>> losses =
        ParallelMapi(expected,
                     [&](int idx, const std::vector<float> &exp) {
                       std::vector<float> got;
                       got.resize(exp.size());
                       training->ExportOutput(idx, &got);

                       float loss = 0.0f;
                       for (int i = 0; i < exp.size(); i++) {
                         loss += fabsf(exp[i] - got[i]);
                       }

                       auto MaxChar = [](const std::vector<float> &v) {
                           CHECK(v.size() == MAX_WORD_LEN * RADIX);
                           string s;
                           s.reserve(MAX_WORD_LEN);
                           for (int c = 0; c < MAX_WORD_LEN; c++) {
                             int maxi = 0;
                             float maxv = -999999.0f;
                             for (int a = 0; a < RADIX; a++) {
                               int idx = c * RADIX + a;
                               if (v[idx] > maxv) {
                                 maxi = a;
                                 maxv = v[idx];
                               }
                             }
                             if (maxi == 0) s.push_back('_');
                             else s.push_back('a' + (maxi - 1));
                           }
                           return s;
                         };
                       string sexp = MaxChar(exp);
                       string sgot = MaxChar(got);

                       if (idx == 0) {
                         // careful about thread safety
                         example_correct = sexp;
                         example_predicted = sgot;
                       }

                       CHECK(sexp.size() == sgot.size());
                       int incorrect = 0;
                       for (int i = 0; i < sexp.size(); i++)
                         if (sexp[i] != sgot[i]) incorrect++;

                       return std::make_pair(loss, incorrect);
                     }, max_parallelism);

      if (VERBOSE > 1)
        printf("Got losses.\n");

      float min_loss = 1.0f / 0.0f, average_loss = 0.0f, max_loss = 0.0f;
      int min_inc = net->layers.back().num_nodes + 1, max_inc = 0;
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

      const double total_sec = train_timer.MS() / 1000.0;
      const double eps = total_examples / total_sec;

      printf("%d: %.3f<%.3f<%.3f", iter,
             min_loss, average_loss, max_loss);
      printf(" | %d<%.3f<%d",
             min_inc, average_inc, max_inc);
      printf(" (%.2f eps)\n", eps);
      printf("   [%s] got [%s]\n",
             example_correct.c_str(), example_predicted.c_str());

      if ((total_verbose % HISTORY_EVERY_VERBOSE) == 0) {
        error_history.Add(net->rounds, average_loss, false);
      }
      total_verbose++;
    }

    if ((iter % IMAGE_EVERY) == 0) {

      // XXX would be better if this was more accurate,
      // but we only want to read from GPU if we're going to
      // actually do anything below
      if (images.size() >= 2 &&
          images[1].get() != nullptr &&
          image_x < images[1]->Width()) {

        net_gpu->ReadFromGPU();

        for (int target_layer = 1; target_layer < net->layers.size();
             target_layer++) {
          ImageRGBA *image = images[target_layer].get();
          if (image == nullptr) continue;
          if (image_x >= image->Width()) continue;

          CHECK(net->layers.size() > 0);
          CHECK(target_layer < net->layers.size());
          const Layer &layer = net->layers[target_layer];
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
            image->BlendPixel32(image_x, ToScreenY(1), 0xCCFFCC40);
            image->BlendPixel32(image_x, ToScreenY(0), 0xCCCCFFFF);
            image->BlendPixel32(image_x, ToScreenY(-1), 0xFFCCCC40);
          }

          uint8 weight_alpha =
            std::clamp((255.0f / sqrtf(chunk.weights.size())), 10.0f, 240.0f);

          for (float w : chunk.weights) {
            // maybe better to AA this?
            image->BlendPixel32(image_x, ToScreenY(w),
                                0xFFFFFF00 | weight_alpha);
          }

          uint8 bias_alpha =
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
            // Also bias aux?
          }


          if ((image_x % 100 == 0) || image_x == image->Width()) {
            string filename = StringPrintf("train-image-%d.png",
                                           target_layer);
            image->Save(filename);
            printf("Wrote %s\n", filename.c_str());
          }
        }
        image_x++;
      }
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
      const string file = StringPrintf("lex.val", iter);
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
      double accounted_ms = example_ms + forward_ms + error_ms +
        decay_ms + backward_ms + update_ms;
      double other_ms = round_ms - accounted_ms;
      double pct = 100.0 / round_ms;
      printf("%.1f%% ex  "
             "%.1f%% fwd  "
             "%.1f%% err  "
             "%.1f%% dec  "
             "%.1f%% bwd  "
             "%.1f%% up  "
             "%.1f%% other\n",
             example_ms * pct,
             forward_ms * pct,
             error_ms * pct,
             decay_ms * pct,
             backward_ms * pct,
             update_ms * pct,
             other_ms * pct);
      double msr = 1.0 / (iter + 1);
      printf("%.1fms ex  "
             "%.1fms fwd  "
             "%.1fms err  "
             "%.1fms dec  "
             "%.1fms bwd  "
             "%.1fms up  "
             "%.1fms other\n",
             example_ms * msr,
             forward_ms * msr,
             error_ms * msr,
             decay_ms * msr,
             backward_ms * msr,
             update_ms * msr,
             other_ms * msr);
    }
  }
}


// Create an Nx1 convolutional chunk. It reads the entire previous
// layer of size prev_size.
static Chunk ConvolutionalChunk1D(int prev_size,
                                  int num_features,
                                  int x_stride,
                                  int pattern_width) {
  Chunk chunk;
  chunk.type = CHUNK_CONVOLUTION_ARRAY;
  chunk.num_features = num_features;
  chunk.occurrence_x_stride = x_stride;
  chunk.occurrence_y_stride = 1;
  chunk.pattern_width = pattern_width;
  chunk.pattern_height = 1;
  chunk.src_width = prev_size;
  chunk.src_height = 1;
  chunk.transfer_function = LEAKY_RELU;
  chunk.span_start = 0;
  chunk.span_size = prev_size;
  chunk.indices_per_node = pattern_width;

  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(0, prev_size,
                                           chunk.num_features,
                                           chunk.pattern_width,
                                           chunk.pattern_height,
                                           chunk.src_width,
                                           chunk.src_height,
                                           chunk.occurrence_x_stride,
                                           chunk.occurrence_y_stride);
    chunk.num_nodes = this_num_nodes;
    chunk.width = chunk.num_nodes;
    chunk.height = 1;
    chunk.channels = 1;

    chunk.num_occurrences_across = num_occurrences_across;
    CHECK(num_occurrences_down == 1);
    chunk.num_occurrences_down = num_occurrences_down;
    chunk.indices = indices;

    chunk.weights = std::vector<float>(
        chunk.indices_per_node * chunk.num_features,
        0.0f);
    chunk.biases = std::vector<float>(chunk.num_features, 0.0f);
  }

  chunk.weight_update = ADAM;
  chunk.weights_aux.resize(chunk.weights.size() * 2, 0.0f);
  chunk.biases_aux.resize(chunk.biases.size() * 2, 0.0f);

  return chunk;
}

static Chunk DenseChunk(int prev_size,
                        int this_size) {
  Chunk chunk;
  chunk.type = CHUNK_DENSE;
  chunk.num_nodes = this_size;
  chunk.transfer_function = LEAKY_RELU;
  chunk.width = this_size;
  chunk.height = 1;
  chunk.channels = 1;
  chunk.span_start = 0;
  chunk.span_size = prev_size;
  chunk.indices_per_node = prev_size;
  chunk.indices = {};
  chunk.weights = std::vector<float>(
      chunk.num_nodes * chunk.indices_per_node, 0.0f);
  chunk.biases = std::vector<float>(chunk.num_nodes, 0.0f);

  chunk.weight_update = ADAM;
  chunk.weights_aux.resize(chunk.weights.size() * 2, 0.0f);
  chunk.biases_aux.resize(chunk.biases.size() * 2, 0.0f);

  return chunk;
}

static Network *NewNetwork() {
  auto L = [&](const Chunk &chunk) {
      return Layer{.num_nodes = chunk.num_nodes, .chunks = {chunk}};
    };

  constexpr int INPUT_SIZE = MAX_WORD_LEN * RADIX;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // Convolve to 10 bits/char. We should only need 5...
  constexpr int BITS_PER_CHAR = 10;
  Chunk conv_chunk1 = ConvolutionalChunk1D(INPUT_SIZE,
                                           BITS_PER_CHAR,
                                           // non-overlapping
                                           RADIX, RADIX);

  // Then bigrams.
  // It would be useful to support overlap here. But I don't know
  // how to expand the thing afterwards, since we'd end up with
  // MAX_WORD_LEN - 1 bigrams in that case?

  // probably also more than we should need
  constexpr int BITS_PER_BIGRAM = 15;
  Chunk conv_chunk2 = ConvolutionalChunk1D(BITS_PER_CHAR * MAX_WORD_LEN,
                                           BITS_PER_BIGRAM,
                                           BITS_PER_CHAR * 2,
                                           BITS_PER_CHAR * 2);

  // Number of nodes in output of previous
  static_assert((MAX_WORD_LEN & 1) == 0);
  constexpr int PRE_ENCODED_WIDTH = (MAX_WORD_LEN / 2) * BITS_PER_BIGRAM;
  CHECK(conv_chunk2.num_nodes == PRE_ENCODED_WIDTH);

  constexpr int ENCODED = 64;
  Chunk dense_encode = DenseChunk(PRE_ENCODED_WIDTH, ENCODED);

  // And now the reverse.
  Chunk dense_decode = DenseChunk(ENCODED, PRE_ENCODED_WIDTH);

  Chunk unconv_chunk2 = ConvolutionalChunk1D(PRE_ENCODED_WIDTH,
                                             BITS_PER_CHAR * 2,
                                             // non-overlapping
                                             BITS_PER_BIGRAM,
                                             BITS_PER_BIGRAM);
  Chunk unconv_chunk1 = ConvolutionalChunk1D(BITS_PER_CHAR * MAX_WORD_LEN,
                                             RADIX,
                                             BITS_PER_CHAR,
                                             BITS_PER_CHAR);


  return new Network(vector<Layer>{
                         L(input_chunk),
                         L(conv_chunk1),
                         L(conv_chunk2),
                         L(dense_encode),
                         L(dense_decode),
                         L(unconv_chunk2),
                         L(unconv_chunk1)});
}


int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile("lex.val"));

  if (net.get() == nullptr) {
    net.reset(NewNetwork());
    net->StructuralCheck();
    ArcFour rc("new");
    RandomizeNetwork(&rc, net.get(), 2);
    printf("New network with %lld parameters\n", net->TotalParameters());
  }

  Train(net.get());

  printf("OK\n");
  return 0;
}
