// Another toy problem.
// Try predicting the middle word using a simple one-hot dictionary
// of words.

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
#include "wikipedia.h"
#include "error-history.h"

#include "direct-word-problem.h"

using namespace std;

using TestNet = NetworkTestUtil::TestNet;
using TrainNet = NetworkTestUtil::TrainNet;
using TestExample = NetworkTestUtil::TestExample;

static CL *cl = nullptr;

using int64 = int64_t;

static bool OkChars(const string &s) {
  if (s.empty()) return false;
  if (s[0] == '\'') return false;
  for (int i = 0; i < s.size(); i++) {
    if (s[i] == '\'' ||
        (s[i] >= 'a' && s[i] <= 'z')) {
      // ok
    } else {
      return false;
    }
  }
  return true;
}

struct Wikibits {
  static constexpr int NUM_SHARDS = 128;
  // static constexpr int NUM_SHARDS = 1;

  Wikibits(Wordlist wordlist) :
    wordlist(std::move(wordlist)),
    rc("wikibits" + StringPrintf("%lld", time(nullptr))) {

    Init();
  }

  void Init() {
    std::vector<string> filenames;
    for (int i = 0; i < NUM_SHARDS; i++)
      filenames.push_back(StringPrintf("wikibits/wiki-%d.txt", i));
    // Sequences of words from wordlists, represented by their
    // indices.
    std::vector<std::vector<std::vector<int>>> processed =
      ParallelMap(filenames,
                  [this](const std::string &filename) {
                    printf("Reading %s...\n", filename.c_str());
                    std::vector<std::vector<int>> frags;
                    int64 not_valid = 0;
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
                      // TODO: Other normalization here (remove outside
                      // double-quotes, maybe rewrite internal apostrophes?
                      for (string &s : tokens) {
                        s = Util::lcase(s);
                        // XXX TODO: This should actually end the fragment
                        if (s.back() == '.') s.pop_back();
                        else if (s.back() == ',') s.pop_back();
                        else if (s.back() == ';') s.pop_back();
                      }

                      static constexpr int MIN_FRAG =
                        WORDS_BEFORE + WORDS_AFTER + 1;

                      std::vector<int> frag;
                      auto MaybeFlushFragment = [&frags, &frag]() {
                          if (frag.size() >= MIN_FRAG) {
                            frags.push_back(std::move(frag));
                          }
                          frag.clear();
                        };

                      for (int i = 0; i < tokens.size(); i++) {
                        const string &token = tokens[i];
                        int id = this->wordlist.GetId(token);
                        if (id >= 0) {
                          frag.push_back(id);
                        } else {
                          not_valid++;
                          MaybeFlushFragment();
                        }
                      }
                      MaybeFlushFragment();
                      num_articles++;
                    }

                    printf("Not valid: %lld, Fragments: %lld\n",
                           not_valid, frags.size());
                    return frags;
                  }, 8);

    printf("Merge fragments:\n");
    for (auto &shard_frags : processed) {
      for (auto &f : shard_frags) {
        fragments.push_back(std::move(f));
      }
    }

    printf("Got %lld total fragments.\n", fragments.size());

    processed.clear();

    for (int i = 0; i < 10; i++) {
      int64 f = RandTo(&rc, fragments.size());
      printf("Fragment %lld:", f);
      for (const int w : fragments[f]) {
        CHECK(w >= 0 && w < wordlist.NumWords());
        printf(" %s", wordlist.GetWord(w).c_str());
      }
      printf("\n");
    }
  }

  // XXX not thread safe
  // XXX this is biased towards selecting subfragments that appear in
  // short fragments.
  // A random fragment of length WORDS_BEFORE + WORDS_AFTER + 1,
  // as the vector and the start position within it.
  const std::pair<int, const std::vector<int>*> RandomFragment() {
    const std::vector<int> &v = fragments[
        RandTo(&rc, fragments.size())];
    const int max_start = v.size() - (WORDS_BEFORE + WORDS_AFTER + 1);
    const int start = RandTo32(&rc, max_start + 1);
    return make_pair(start, &v);
  }

  inline const std::string &GetWord(int id) const {
    CHECK(id >= 0 && id < wordlist.NumWords());
    return wordlist.GetWord(id);
  }

private:
  const Wordlist wordlist;

  // Sequences of word ids where:
  //  - the id is in the wordlist
  //  - the sequence is at least WORDS_BEFORE + WORDS_AFTER + 1 in length
  std::vector<std::vector<int>> fragments;

  ArcFour rc;
};

// Examples are 64k * num_words ~1.5MB each, although they are
// extremely sparse.
static constexpr int EXAMPLES_PER_ROUND = 1000;

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

  ExampleThread(Wikibits *wikibits) : wikibits(wikibits) {
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
        example_words.reserve(EXAMPLES_PER_ROUND * NUM_WORDS);
        for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
          const auto &[start_pos, fragment] = wikibits->RandomFragment();
          CHECK(start_pos + WORDS_BEFORE + 1 + (WORDS_AFTER - 1) <
                fragment->size());

          // For simplicity we put the target word at the end.
          for (int w = 0; w < WORDS_BEFORE; w++) {
            example_words.push_back((*fragment)[start_pos + w]);
          }
          for (int w = 0; w < WORDS_AFTER; w++) {
            example_words.push_back(
                (*fragment)[start_pos + WORDS_BEFORE + 1 + w]);
          }
          example_words.push_back((*fragment)[start_pos + WORDS_BEFORE]);
        }

        CHECK(example_words.size() == EXAMPLES_PER_ROUND * NUM_WORDS);
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

  Wikibits *wikibits = nullptr;
};

static void Train(Network *net) {
  Wikibits wikibits(Wordlist{});

  ExampleThread example_thread(&wikibits);

  ErrorHistory error_history("direct-words-error-history.tsv");

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;
  // XXX need to reduce this over time
  static constexpr float LEARNING_RATE = 0.00500f;
  // This is very conservative, but with larger values I would
  // get divergence after hundreds of thousands of rounds.
  static constexpr float ADAM_EPSILON = 0.001f;

  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  static constexpr float DECAY_RATE = 0.999999f;

  // On a verbose round we compute training error and print out
  // examples.
  constexpr int VERBOSE_EVERY = 100;
  // We save this to the error history file every this many
  // verbose rounds.
  constexpr int HISTORY_EVERY_VERBOSE = 10;
  int64 total_verbose = 0;
  constexpr int TIMING_EVERY = 1000;

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

    // XXX decay disabled.
    if (false) {
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
      std::vector<std::pair<float, int>> losses =
        ParallelMapi(expected,
                     [&](int idx, const std::vector<float> &exp) {
                       std::vector<float> got;
                       got.resize(exp.size());
                       CHECK(got.size() == OUTPUT_SIZE);
                       CHECK(idx >= 0 && idx < EXAMPLES_PER_ROUND);
                       training->ExportOutput(idx, &got);

                       // TODO: Compute loss for the auto-encoded
                       // words, and for the predicted word
                       float loss = 0.0f;
                       for (int i = 0; i < exp.size(); i++) {
                         loss += fabsf(exp[i] - got[i]);
                       }

                       std::vector<int> words_exp =
                         DirectWordProblem::DecodeOutput(exp);
                       std::vector<int> words_got =
                         DirectWordProblem::DecodeOutput(got);

                       CHECK(words_exp.size() == words_got.size());
                       int incorrect = 0;
                       for (int i = 0; i < words_exp.size(); i++) {
                         if (words_exp[i] != words_got[i]) {
                           incorrect++;
                         }
                       }

                       if (idx == 0) {
                         auto Word = [&](int id) -> string {
                             if (id == -1) {
                               return "-";
                             } else {
                               return wikibits.GetWord(id);
                             }
                           };

                         // careful about thread safety
                         for (int i = 0; i < WORDS_BEFORE; i++) {
                           if (i) {
                             example_correct += " ";
                             example_predicted += " ";
                           }
                           example_correct += Word(words_exp[i]);
                           example_predicted += Word(words_got[i]);
                         }
                         StringAppendF(
                             &example_correct,
                             " (%s)",
                             Word(words_exp[WORDS_BEFORE +
                                            WORDS_AFTER]).c_str());
                         StringAppendF(
                             &example_predicted,
                             " (%s)",
                             Word(words_got[WORDS_BEFORE +
                                            WORDS_AFTER]).c_str());
                         for (int i = 0; i < WORDS_AFTER; i++) {
                           example_correct += " ";
                           example_predicted += " ";
                           example_correct +=
                             Word(words_exp[WORDS_BEFORE + i]);
                           example_predicted +=
                             Word(words_got[WORDS_BEFORE + i]);
                         }
                       }

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

static Network *NewNetwork() {
  ArcFour rc("learn-words-network");
  // Input is a vector of one-hot words. We don't get the word to predict
  // as an input!
  constexpr int INPUT_WORDS = WORDS_BEFORE + WORDS_AFTER;
  constexpr int INPUT_SIZE = INPUT_WORDS * WORDLIST_SIZE;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // We want to process all the words the same way to build the
  // semantic encodings.

  // [   wb1  ][   wb2  ]?[   wa1  ][   wa2  ]
  //  |......|  |......|   |......|  |......|   conv1
  //  |......|  |......|   |......|  |......|   conv2
  //  |......|  |......|   |......|  |......|   conv3

  // ? is where the missing word would go. But to simplify our lives,
  // we will actually put the missing word at the end.

  // We're just using a "convolution" here so that we have the same
  // weights across the words. Note that these have to be effectively
  // dense, as that is the only kind of convolution we support.

  auto ConvChunk = [&rc](int input_word_size,
                         int num_words,
                         int output_word_size) {
      Chunk chunk;
      chunk.type = CHUNK_CONVOLUTION_ARRAY;
      chunk.num_features = output_word_size;
      chunk.occurrence_x_stride = input_word_size;
      chunk.occurrence_y_stride = 1;
      chunk.pattern_width = input_word_size;
      chunk.pattern_height = 1;
      chunk.src_width = input_word_size * num_words;
      chunk.src_height = 1;
      chunk.transfer_function = LEAKY_RELU;
      chunk.span_start = 0;
      chunk.span_size = input_word_size * num_words;
      chunk.indices_per_node = input_word_size;

      {
        const auto [indices, this_num_nodes,
                    num_occurrences_across, num_occurrences_down] =
          Network::MakeConvolutionArrayIndices(chunk.span_start,
                                               chunk.span_size,
                                               chunk.num_features,
                                               chunk.pattern_width,
                                               chunk.pattern_height,
                                               chunk.src_width,
                                               chunk.src_height,
                                               chunk.occurrence_x_stride,
                                               chunk.occurrence_y_stride);
        CHECK(this_num_nodes == num_words * output_word_size);
        chunk.num_nodes = this_num_nodes;
        chunk.width = chunk.num_nodes;
        chunk.height = 1;
        chunk.channels = 1;

        CHECK(num_occurrences_across == num_words);
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
    };

  // The input is very sparse (64k wide, one-hot). So we want to
  // collapse to a narrow embedding quickly. This first layer
  // basically just stores the embedding as a table.

  constexpr int ENC_SIZE = 64;
  Chunk conv_chunk1 =
    ConvChunk(WORDLIST_SIZE, INPUT_WORDS, ENC_SIZE);


  // [   wb1  ][   wb2  ]?[   wa1  ][   wa2  ] [ target ]
  //  |......|  |......|   |......|  |......|
  //  |......|  |......|   |......|  |......|
  //  |......|  |......|   |......|  |......|
  //  [ copy      copy       copy      copy ]  [  guess1  ]
  //  [ copy      copy       copy      copy ]  [  guess2  ]
  //  [ copy      copy       copy      copy ]  [  guess3  ]
  //  [ copy      copy       copy      copy ]  [   sem    ]
  // Now we want to start predicting the middle word. We actually
  // put the middle word at the end to make the structure simpler. The first
  // chunk is a copy of all the semantic embeddings for the words,
  // and these chunks are fixed. The new column uses those words
  // as inputs, as well as the previous guess layer.
  // Although it may still be fun, the real goal here is to
  // learn an embedding of words that makes this guess possible
  // (or at least, to reduce its error). So we don't just want
  // this part to memorize triples; a more constrained network
  // may be preferable.

  // We can actually use this same chunk over and over.
  Chunk copy_chunk;
  copy_chunk.type = CHUNK_SPARSE;
  copy_chunk.fixed = true;
  copy_chunk.span_start = 0;
  copy_chunk.span_size = ENC_SIZE * INPUT_WORDS;
  copy_chunk.num_nodes = ENC_SIZE * INPUT_WORDS;
  copy_chunk.indices_per_node = 1;
  copy_chunk.transfer_function = IDENTITY;
  copy_chunk.weight_update = SGD;
  copy_chunk.width = ENC_SIZE * INPUT_WORDS;
  copy_chunk.height = 1;
  copy_chunk.channels = 1;
  for (int i = 0; i < ENC_SIZE * INPUT_WORDS; i++) {
    copy_chunk.indices.push_back(i);
    copy_chunk.weights.push_back(1.0f);
    copy_chunk.biases.push_back(0);
  }

  constexpr int GUESS1_SIZE = 128;
  constexpr int GUESS2_SIZE = 256;
  constexpr int GUESS3_SIZE = 64;
  static_assert(GUESS3_SIZE == ENC_SIZE);
  Chunk guess_chunk1;
  guess_chunk1.type = CHUNK_DENSE;
  guess_chunk1.fixed = false;
  guess_chunk1.span_start = 0;
  guess_chunk1.span_size = ENC_SIZE * INPUT_WORDS;
  guess_chunk1.num_nodes = GUESS1_SIZE;
  guess_chunk1.indices_per_node = guess_chunk1.span_size;
  guess_chunk1.transfer_function = LEAKY_RELU;
  guess_chunk1.weight_update = ADAM;
  guess_chunk1.width = GUESS1_SIZE;
  guess_chunk1.height = 1;
  guess_chunk1.channels = 1;
  guess_chunk1.weights.resize(GUESS1_SIZE * ENC_SIZE * INPUT_WORDS, 0.0f);
  guess_chunk1.biases.resize(GUESS1_SIZE, 0.0f);
  guess_chunk1.weights_aux.resize(guess_chunk1.weights.size() * 2, 0.0f);
  guess_chunk1.biases_aux.resize(guess_chunk1.biases.size() * 2, 0.0f);

  auto SparseGuess = [&](int guess_size,
                         int prev_layer_size,
                         float density) {
      int ipn = density * prev_layer_size;
      CHECK(ipn > 0);
      CHECK(ipn <= prev_layer_size);
      Chunk chunk;
      chunk.type = CHUNK_SPARSE;
      chunk.fixed = false;
      chunk.span_start = 0;
      chunk.span_size = prev_layer_size;
      chunk.num_nodes = guess_size;
      chunk.indices_per_node = ipn;
      chunk.transfer_function = LEAKY_RELU;
      chunk.weight_update = ADAM;
      chunk.width = guess_size;
      chunk.height = 1;
      chunk.channels = 1;
      chunk.weights.resize(guess_size * ipn, 0.0f);
      chunk.biases.resize(guess_size, 0.0f);
      chunk.weights_aux.resize(chunk.weights.size() * 2, 0.0f);
      chunk.biases_aux.resize(chunk.biases.size() * 2, 0.0f);
      for (int n = 0; n < chunk.num_nodes; n++) {
        // Add random indices.
        vector<uint32_t> all_indices;
        for (int i = 0; i < prev_layer_size; i++) all_indices.push_back(i);
        Shuffle(&rc, &all_indices);
        all_indices.resize(ipn);
        std::sort(all_indices.begin(), all_indices.end());
        for (uint32_t idx : all_indices)
          chunk.indices.push_back(idx);
      }
      return chunk;
    };

  Chunk guess_chunk2 = SparseGuess(GUESS2_SIZE,
                                   ENC_SIZE * INPUT_WORDS + GUESS1_SIZE,
                                   0.25f);
  Chunk guess_chunk3 = SparseGuess(GUESS3_SIZE,
                                   ENC_SIZE * INPUT_WORDS + GUESS2_SIZE,
                                   0.25f);

  // Now we have INPUT_WORDS + 1 words. Decode the embedding.
  constexpr int OUTPUT_WORDS = INPUT_WORDS + 1;

  // Here we can't just tabluate in a single layer to get the
  // one-hot representation; use a few layers for some computation.
  constexpr int DEC3_SIZE = 128;
  constexpr int DEC2_SIZE = 256;
  constexpr int DEC1_SIZE = WORDLIST_SIZE;
  static_assert(DEC1_SIZE == WORDLIST_SIZE);
  Chunk unconv_chunk3 =
    ConvChunk(ENC_SIZE, OUTPUT_WORDS, DEC3_SIZE);
  Chunk unconv_chunk2 =
    ConvChunk(DEC3_SIZE, OUTPUT_WORDS, DEC2_SIZE);
  Chunk unconv_chunk1 =
    ConvChunk(DEC2_SIZE, OUTPUT_WORDS, DEC1_SIZE);

  unconv_chunk1.transfer_function = SIGMOID;

  auto L = [&](const std::vector<Chunk> &chunks) {
      int num_nodes = 0;
      for (const Chunk &chunk : chunks) num_nodes += chunk.num_nodes;
      return Layer{.num_nodes = num_nodes, .chunks = chunks};
    };

  std::vector<Layer> layers = {
    L({input_chunk}),
    L({conv_chunk1}),
    L({copy_chunk, guess_chunk1}),
    L({copy_chunk, guess_chunk2}),
    L({copy_chunk, guess_chunk3}),
    L({unconv_chunk3}),
    L({unconv_chunk2}),
    L({unconv_chunk1}),
  };

  return new Network(layers);
}


int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  if (net.get() == nullptr) {
    net.reset(NewNetwork());
    net->StructuralCheck();
    ArcFour rc("new");
    RandomizeNetwork(&rc, net.get(), 2);
    printf("New network with %lld parameters\n", net->TotalParameters());
  }

  net->StructuralCheck();
  net->NaNCheck(MODEL_NAME);

  Train(net.get());

  printf("OK\n");
  return 0;
}
