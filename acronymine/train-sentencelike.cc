// Convolutional model for predicting the probability that some
// word sequence comes from Wikipedia. This one uses part-of-speech
// info (wordnet) and word frequency information.

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
#include "lastn-buffer.h"
#include "color-util.h"

#include "acronym-util.h"
#include "word2vec.h"
#include "wikipedia.h"
#include "freq.h"
#include "wordnet.h"

using namespace std;

static constexpr const char *WIKIPEDIA_FILE =
  // "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

static constexpr const char *WORD2VEC_FILE =
  "c:\\code\\word2vec\\GoogleNews-vectors-negative300.bin";
static constexpr const char *WORD2VEC_FILL_FILE = "word2vecfill.txt";

static constexpr const char *WORDNET_DIR = "wordnet";

static constexpr const char *FREQ_FILE = "freq.txt";
static constexpr const char *WORDLIST_FILE = "word-list.txt";

static CL *cl = nullptr;

using int64 = int64_t;

static constexpr WeightUpdate WEIGHT_UPDATE = ADAM;

#define MODEL_BASE "sentencelike"
#define MODEL_NAME MODEL_BASE ".val"

static constexpr int VEC_SIZE = 300;
static constexpr int WORDNET_PROPS = 13;
static_assert(WORDNET_PROPS == WordNet::NUM_PROPS,
              "can't change an existing model, though");
static constexpr int PHRASE_SIZE = 8;

// w2v vector, normalized frequency, wordnet props
static constexpr int ONE_WORD_SIZE = (VEC_SIZE + 1 + WORDNET_PROPS);

static constexpr int INPUT_SIZE = PHRASE_SIZE * ONE_WORD_SIZE;
static constexpr int OUTPUT_SIZE = 1;

static constexpr int EXAMPLES_PER_ROUND = 1024;

static std::unordered_set<std::string> *words = nullptr;
static Word2Vec *w2v = nullptr;
static Freq *freq = nullptr;
static WordNet *wordnet = nullptr;

static void LoadData() {
  {
    int max_word = 0;
    vector<string> wordlist =
      Util::ReadFileToLines("word-list.txt");
    words = new unordered_set<string>;
    for (string &s : wordlist) {
      max_word = std::max(max_word, (int)s.size());
      words->insert(std::move(s));
    }
    printf("%d words in dictionary\n", (int)words->size());
    printf("Longest word: %d\n", max_word);
  }

  InParallel(
      [](){
        w2v = Word2Vec::Load(WORD2VEC_FILE, WORD2VEC_FILL_FILE, *words);
        CHECK(w2v != nullptr) << WORD2VEC_FILE;
      },
      [](){
        freq = Freq::Load(FREQ_FILE, *words);
        CHECK(freq != nullptr) << FREQ_FILE;
      },
      []() {
        wordnet = WordNet::Load(WORDNET_DIR, *words);
        CHECK(wordnet != nullptr) << WORDNET_DIR;
      });

  printf("Done loading.\n"
         "Word2Vec: %d words (vec %d floats)\n"
         "WordNet: %d words (%d props)\n",
         (int)w2v->NumWords(), w2v->Size(),
         wordnet->NumWords(), WordNet::NUM_PROPS);
}

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
  tparams.update_config.base_learning_rate = 0.1f;
  tparams.update_config.learning_rate_dampening = 0.25f;
  tparams.update_config.adam_epsilon = 1.0e-5;
  tparams.update_config.constrain = true;
  tparams.update_config.weight_constrain_max = 16.0f;
  tparams.update_config.bias_constrain_max = 8760.0f;
  tparams.update_config.clip_error = true;
  tparams.update_config.error_max = 1000.0f;

  tparams.do_decay = false;
  return tparams;
}

// Load the floats for the phrase on the end of the inputs and ouputs.
static void LoadPredExample(
    const std::array<uint32_t, PHRASE_SIZE> &phrase,
    bool is_sentence,
    std::vector<float> *inputs,
    std::vector<float> *outputs) {

  for (int j = 0; j < PHRASE_SIZE; j++) {
    uint32_t w = phrase[j];
    const string &word = w2v->WordString(w);
    // word2vec vector; required
    for (float f : w2v->NormVec(w)) {
      inputs->push_back(f);
    }
    // frequency
    inputs->push_back(freq->NormalizedFreq(word));
    // wordnet props
    uint32_t props = wordnet->GetProps(word);
    for (int i = 0; i < WORDNET_PROPS; i++) {
      inputs->push_back((props & (1 << i)) ? 1.0f : 0.0f);
    }
  }

  // Output label is either -1 or +1.
  outputs->push_back(is_sentence ? 1.0f : -1.0f);
}

struct ExampleThread {
  // Need multiple types of examples:
  //   For the prediction model:
  //     Real phrases from wikipedia -> 1.0
  //     Made up phrase -> -1.0

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
    CHECK(w2v != nullptr);

    work_thread1.reset(new std::thread(&Populate, this));
    // Can support multiple generation threads, but it is very
    // fast for this problem.
    work_thread2.reset(new std::thread(&Generate, this, 1));
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
  // Number of rounds we try to be ahead by.
  static constexpr int TARGET_SIZE = 10;

  std::mutex pool_mutex;
  std::vector<std::array<uint32_t, PHRASE_SIZE>> phrase_pool;

  static void Normalize(Wikipedia *wikipedia, Wikipedia::Article *art) {
    art->body = wikipedia->ReplaceEntities(std::move(art->body));
    art->body = wikipedia->RemoveTags(art->body);
    wikipedia->RemoveWikilinks(&art->body);
    wikipedia->RemoveMarkup(&art->body);
    wikipedia->ASCIIify(&art->body);
  }

  // Fills the pool from the wikipedia data.
  void Populate() {
    std::unique_ptr<Wikipedia> wikipedia(
        Wikipedia::Create(WIKIPEDIA_FILE));
    CHECK(wikipedia.get() != nullptr) << WIKIPEDIA_FILE;

    auto OneArticle = [this, &wikipedia](Wikipedia::Article *article) {
        Normalize(wikipedia.get(), article);

        if (wikipedia->IsRedirect(*article))
          return;

        std::vector<string> article_words =
          Util::Tokens(article->body,
                       [](char c) {
                         // Break on anything not a-z0-9.
                         return !std::isalnum(c);
                       });

        std::vector<std::array<uint32_t, PHRASE_SIZE>> phrases =
          AcronymUtil::GetPhrases<PHRASE_SIZE>(w2v, article_words);

        {
          MutexLock ml(&pool_mutex);
          for (auto &a : phrases) {
            phrase_pool.emplace_back(std::move(a));
          }
        }
      };

    Periodically status_per(10.0);
    Timer timer;
    // Parallelize processing with disk I/O.
    {
      Asynchronously async(4);
      for (int64 num_articles = 0; true; num_articles++) {
        auto ao = wikipedia->Next();
        if (!ao.has_value()) break;

        async.Run([&OneArticle, article = std::move(ao.value())]() mutable {
            OneArticle(&article);
          });
        if (status_per.ShouldRun()) {
          double total_sec = timer.Seconds();
          printf("Wikipedia: %lld  [%.2f/sec]\n", num_articles,
                 num_articles / total_sec);
        }

        // Check if we should exit early.
        {
          MutexLock ml(&m);
          if (examplethread_should_die)
            return;
        }
      }
    }

    {
      MutexLock ml(&pool_mutex);
      phrase_pool.shrink_to_fit();
    }

    printf("Wikipedia: Done in %.2f sec\n", timer.Seconds());
  }

  void Generate(int id) {
    printf("Started example thread %d.\n", id);

    // Not deterministic.
    ArcFour rc(StringPrintf("%d.%lld.ex", id, time(nullptr)));
    RandomGaussian gauss(&rc);

    for (;;) {
      {
        size_t pool_size = 0;
        {
          MutexLock ml(&pool_mutex);
          pool_size = phrase_pool.size();
        }
        if (pool_size > 10000)
          break;
        printf("Wait for enough examples (only %d) ...\n", (int)pool_size);
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      {
        MutexLock ml(&m);
        if (examplethread_should_die)
          return;
      }
    }

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
        static_assert(EXAMPLES_PER_ROUND % 2 == 0);
        for (int i = 0; i < EXAMPLES_PER_ROUND; i += 2) {

          // Real example
          {
            std::array<uint32_t, PHRASE_SIZE> real_phrase;
            {
              MutexLock ml(&pool_mutex);
              const int idx = RandTo(&rc, phrase_pool.size());
              real_phrase = phrase_pool[idx];
            }

            LoadPredExample(real_phrase, true, &examples, &outputs);
          }

          // Noise example
          {
            std::array<uint32_t, PHRASE_SIZE> noise_phrase;
            for (int w = 0; w < PHRASE_SIZE; w++) {
              const int idx = [&rc]() {
                  // Some words are not in word2vec, so keep sampling until
                  // we get ones that are.
                  // PERF: We could have prepared our own vector (of uint32)
                  // instead of using Freq here?
                  for (;;) {
                    int64_t sample_idx = RandTo(&rc, freq->TotalCount());
                    std::string word = freq->ByWeightedSampleIndex(sample_idx);
                    const int idx = w2v->GetWord(word);
                    if (idx >= 0)
                      return idx;
                  }
                }();
              noise_phrase[w] = idx;
            }
            LoadPredExample(noise_phrase, false, &examples, &outputs);
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

  std::mutex m;
  // queue of examples, ready to train
  bool examplethread_should_die = false;
  std::deque<example_type> q;

  std::unique_ptr<std::thread> work_thread1, work_thread2;
};

// Evaluate on some fixed test phrases that are not from the
// database.
struct Evaluator {
  std::vector<std::string> good_phrases = {
    "each phrase should be at least as long as the target length "
    "  and every word must be in the dictionary",
    "there should also be no punctuation in the sentences",
    "we do not expect to predict these exactly "
    "  but closer predictions are better nonetheless",
    "a wise man once said that all you have to do is do it",
    "a simple sum is six plus seven equals thirteen",
    "the best camera is the one that you have on you",
    "laser stands for "
      "light amplification by stimulated emission of radiation",
    "one two three four five six seven eight nine ten eleven twelve",
    "twelve eleven ten nine eight seven six five four three two one",
    "it was on this day one hundred years ago that paleontologists "
      "first discovered dinosaur eggs",
    "a micrometer is a device for measuring precise distances",
    "a great sadness fell upon him like a cold breath of winter",
    "now it is the summertime of our discontent",
    "dental floss is used to clean food scraps out from between teeth",
    "a vegetarian is someone who does not eat meat",
    "in order to take the test you need a number two pencil",
    "the car tire was flattened by a large pothole",
    "fog is just clouds that are at ground level",
    "coffee contains a stimulant called caffeine that causes "
      "feelings of wakefulness in mammals",
    "nobody really understands why animals sleep but we know "
      "bad things happen if you do not",
    "it is also important to drink at least two liters of water "
      "every day",
    "if someone has bacterial pneumonia the typical treatment "
      "is antibiotics",
    "during the summer i like to place a fan in my window to "
      "improve airflow",
    "after reading the love letter the boy felt heartsick",
    "there once was a man who ate large metal objects like bicycles "
      "and airplanes",
    "in a large city the fastest way to deliver a physical document "
      "is often by bicycle courier",
    "it stands for compact disc read only memory",
    "this image format was named for the joint photographic experts "
      "group who invented it",
    "some pedantic people make a distinction between an acronym "
      "and an initialism",
    "aspirin is the name given by the "
      "international union of pure and applied chemistry",
    "united states treasury bonds are considered a virtually "
      "risk free investment and are often used as a benchmark "
      "for interest rates",
    "one of the most common occupational hazards is the "
      "repetitive stress injury",
    "please pinch me to test if i am dreaming",
    "two kinds of keyboards are computer keyboards and "
      "musical keyboards",
    "a gig worker is usually classified as an independent "
      "contractor so that they are not paid benefits",
    // Actual phrase from Wikipedia (2023).
    "russian forces improved their focus on the protection "
      "of supply lines by advancing slowly and methodically",
  };

  std::vector<std::string> noise_phrases = {
    "the the the the the no the the the",
    "st william san richard dr los angeles von francisco "
      "joe jim steve kong chris tony francis hong brian alan "
      "roger gordon carl eric juan",
    "centrifuge cere cerebrate certificate certify chafe chaff "
      "chaffer chagrin chain chain chain chair chairman chalk "
      "chalk chalk challenge chamber chamfer champ champion "
      "chance chance chance chandelle change",
    "the of and in a to was is for as by on with that from "
      "at were it his he an or are which be this align had "
      "also has not",
    "wrong verbs order the of word sentence a",
    "semantically entertainment polish soon bay is was be tribes "
      "database use few",
    "narrative navigation varies verse muscle tissue argues "
      "targets ukrainian wealthy prefecture probability ask raid "
      "devoted susan blind feeling mario hughes publishers gauge eat",
    "this would be a goods sentence except for then errors",

    // Actual phrase from wikipedia (2023) but with the word order
    // randomized.
    "methodically by improved and advancing supply russian focus "
      "forces the lines slowly their of protection on",

    // TODO: more! perhaps construct these randomly like in
    // the training data?
  };

  int num_real = 0;
  std::vector<std::pair<std::array<uint32_t, PHRASE_SIZE>, bool>> eval_phrases;
  std::vector<std::pair<int, std::string>> readable_phrases;

  Evaluator(CL *cl, Word2Vec *w2v) : cl(cl), w2v(w2v) {
    auto AddPhrases = [&](const std::vector<std::string> &phrases,
                          bool real) {
        for (int rpidx = 0; rpidx < phrases.size(); rpidx++) {
          const string &rp = phrases[rpidx];
          std::vector<std::string> article_words =
            Util::Tokens(rp,
                         [](char c) {
                           // Break on anything not a-z0-9.
                           return !std::isalnum(c);
                         });

          for (const std::array<uint32_t, PHRASE_SIZE> &a :
                 AcronymUtil::GetPhrases<PHRASE_SIZE>(w2v, article_words)) {
            eval_phrases.push_back(make_pair(a, real));
            std::vector<string> rv;
            rv.reserve(a.size());
            for (uint32_t w : a) {
              rv.push_back(w2v->WordString(w));
            }
            readable_phrases.push_back(make_pair(rpidx, Util::Join(rv, " ")));
          }
        }
      };

    AddPhrases(good_phrases, true);
    num_real = eval_phrases.size();
    AddPhrases(noise_phrases, false);
    CHECK(!eval_phrases.empty());
    CHECK(eval_phrases.size() == readable_phrases.size());
  }

  struct Result {
    double average_loss = 0.0;
    // predicted, expected, phrase
    std::vector<std::tuple<float, bool, int, string>> examples;
    // For a batch.
    double fwd_time = 0.0;
    // "Correct" if we have the same sign.
    int correct_noise = 0, correct_real = 0;
    // Number of examples.
    int total_noise = 0, total_real = 0;
  };

  // Needs non-const network, but doesn't modify it.
  Result Evaluate(Network *net) {
    Result result;

    auto net_gpu = std::make_unique<NetworkGPU>(cl, net);

    std::unique_ptr<ForwardLayerCL> forward_cl =
      std::make_unique<ForwardLayerCL>(cl, net_gpu.get());

    const int NUM_TEST = eval_phrases.size();
    // This could easily be supported by looping!
    CHECK(NUM_TEST <= 1024);
    const int BATCH_SIZE = NUM_TEST;

    // Uninitialized training examples on GPU.
    std::unique_ptr<TrainingRoundGPU> training(
        new TrainingRoundGPU(BATCH_SIZE, cl, *net));

    std::vector<float> inputs;
    inputs.reserve(BATCH_SIZE * INPUT_SIZE);
    std::vector<float> expected_outputs;
    expected_outputs.reserve(BATCH_SIZE * OUTPUT_SIZE);

    for (int i = 0; i < BATCH_SIZE; i++) {
      LoadPredExample(eval_phrases[i].first,
                      eval_phrases[i].second,
                      &inputs, &expected_outputs);
    }

    training->LoadInputs(inputs);

    Timer fwd_timer;
    for (int src_layer = 0;
         src_layer < net->layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training.get(), src_layer);
    }
    result.fwd_time += fwd_timer.Seconds();

    std::vector<float> actual_outputs;
    actual_outputs.resize(BATCH_SIZE * OUTPUT_SIZE);
    training->ExportOutputs(&actual_outputs);

    for (int ex = 0; ex < BATCH_SIZE; ex++) {
      CHECK(OUTPUT_SIZE == 1);
      double loss = fabs(actual_outputs[ex] - expected_outputs[ex]);
      result.average_loss += loss;

      if (actual_outputs[ex] < 0.0 && expected_outputs[ex] < 0.0)
        result.correct_noise++;
      if (actual_outputs[ex] > 0.0 && expected_outputs[ex] > 0.0)
        result.correct_real++;

      result.examples.emplace_back(
          actual_outputs[ex], expected_outputs[ex] > 0.0,
          readable_phrases[ex].first, readable_phrases[ex].second);
    }

    result.average_loss /= NUM_TEST;
    result.total_noise = NUM_TEST - num_real;
    result.total_real = num_real;

    return result;
  }

private:
  CL *cl = nullptr;
  Word2Vec *w2v = nullptr;
};

static void Train(const string &dir, Network *net,
                  TrainParams params) {
  ExampleThread example_thread;

  const string error_history_file = Util::dirplus(dir, "error-history.tsv");
  const string model_file = Util::dirplus(dir, MODEL_NAME);

  ErrorHistory error_history(error_history_file);

  Evaluator evaluator(cl, w2v);

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;

  // On a verbose round we compute training error and print out
  // examples.
  int64 total_verbose = 0;
  Periodically verbose_per(60.0);

  // How often to also save error history to disk (if we run a
  // verbose round). This also evaluates on the test data set
  // simultaneously.
  //
  // Early in training, we do this more often, as the changes
  // are often rapid then.
  const double HISTORY_EVERY_SEC =
    net->rounds < 1000 ? 30.0 : 120.0;
  Periodically history_per(HISTORY_EVERY_SEC);

  static constexpr double TIMING_EVERY_SEC = 60.0;
  Periodically timing_per(TIMING_EVERY_SEC);


  static constexpr int64 CHECKPOINT_EVERY_ROUNDS = 100000;

  constexpr int IMAGE_EVERY = 100;
  TrainingImages images(*net, Util::dirplus(dir, "train"),
                        model_file, IMAGE_EVERY);

  printf("Training!\n");

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
  for (int iter = 0; true; iter++) {
    Timer round_timer;

    const bool verbose_round = verbose_per.ShouldRun();

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

      printf(AYELLOW("%lld") " rounds "
             " | " ABLUE("%.2f") " rounds/min;  "
             APURPLE("%.2f") " eps\n",
             net->rounds, rps * 60.0, eps);

      // compare expecteds (flat; same format) with actuals.
      std::vector<float> actuals(EXAMPLES_PER_ROUND * OUTPUT_SIZE);
      training->ExportOutputs(&actuals);

      const bool save_history = history_per.ShouldRun();

      double avg_loss = 0.0;
      for (int idx = 0; idx < EXAMPLES_PER_ROUND; idx++) {
        CHECK(OUTPUT_SIZE == 1);
        float loss = fabsf(expecteds[idx] - actuals[idx]);
        avg_loss += loss;
      }
      avg_loss /= EXAMPLES_PER_ROUND;

      printf("   Avg loss: %.6f\n", avg_loss);

      if (save_history) {
        constexpr int ERROR_TRAIN = 0;
        constexpr int ERROR_EVAL_LOSS = 1;
        constexpr int ERROR_EVAL_RCORRECT = 2;
        constexpr int ERROR_EVAL_NCORRECT = 3;

        net_gpu->ReadFromGPU();
        Evaluator::Result res = evaluator.Evaluate(net);

        // For each of these a score of 0 means perfect.
        // Loss can be (nominally) as high as 2, so we scale those down.
        error_history.Add(net->rounds, avg_loss * 0.5,
                          ERROR_TRAIN);
        error_history.Add(net->rounds, res.average_loss * 0.5,
                          ERROR_EVAL_LOSS);

        const double real_errors =
          1.0 - (res.correct_real / (double)res.total_real);
        error_history.Add(net->rounds, real_errors,
                          ERROR_EVAL_RCORRECT);

        const double noise_errors =
          1.0 - (res.correct_noise / (double)res.total_noise);
        error_history.Add(net->rounds, noise_errors,
                          ERROR_EVAL_NCORRECT);


        printf(
            "Real (%d/%d) Noise (%d/%d) [train %.5f | eval %.5f] in %.4fs \n",
            res.correct_real, res.total_real,
            res.correct_noise, res.total_noise,
            avg_loss, res.average_loss,
            res.fwd_time);

        error_history.MakeImage(
            1920, 1080,
            {{ERROR_TRAIN, {0x0033FFFF, "train"}},
             {ERROR_EVAL_LOSS, {0xFFAA33FF, "eval"}},
             {ERROR_EVAL_RCORRECT, {0x33FF33FF, "real"}},
             {ERROR_EVAL_NCORRECT, {0xFF3333FF, "noise"}},
            },
            0).Save(Util::dirplus(dir, "error-history.png"));

        #if 0
        auto AnsiHeat = [](float f) {
            uint32_t color = ColorUtil::LinearGradient32(
                ColorUtil::HEATED_METAL, f);
            const auto [r, g, b, a_] = ColorUtil::Unpack32(color);
            return
              StringPrintf("%s%.3f" ANSI_RESET,
                           AnsiForegroundRGB(r, g, b).c_str(),
                           f);
          };
        #endif

        printf(AGREY("pred") " " AGREY("real") "  phrase...\n");
        int last_idx = -1;
        for (const auto &[pred, real, idx, phrase] : res.examples) {
          const bool correct = real ? pred > 0.0f : pred < 0.0f;
          /*
          if (real) {
            if (real_left == 0) continue;
            real_left--;
          } else {
            if (noise_left == 0) continue;
            noise_left--;
          }
          */
          if (idx == last_idx) continue;
          last_idx = idx;

          printf(
              "%s%s" ANSI_RESET " %s %s\n",
              correct ? ANSI_GREEN : ANSI_RED,
              StringPrintf("%.3f", pred).c_str(),
              real ? "Y" : "N",
              phrase.c_str());
        }

        if (net->rounds > 1000)
          history_per.SetPeriod(120.0);
      }

      total_verbose++;
      loss_ms += loss_timer.MS();
    }

    // TODO: Should probably synchronize saving images with saving
    // the model. Otherwise when we continue, we lose pixels that
    // were written to the image but not saved to disk. We now explicitly
    // save when we save the image, so many be should NOT implicitly
    // save inside Sample.
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

  // Note: This should be unreachable as there's no exit criteria any more.
  error_history.Save();
  images.Save();

  net->SaveToFile(model_file);
  printf("Saved to %s.\n", model_file.c_str());
}

static unique_ptr<Network> NewSentencelikeNetwork() {
  // Deterministic!
  ArcFour rc("learn-sentencelike-network");

  std::vector<Layer> layers;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  layers.push_back(Network::LayerFromChunks(input_chunk));

  // All convolutional.

  int prev_blocks = PHRASE_SIZE;
  int prev_features = ONE_WORD_SIZE;

  while (prev_blocks >= 3) {
    int window = 3; // layers.size() > 1 ? 3 : 2;
    int num_features = 1024;
    Chunk conv_chunk =
      Network::Make1DConvolutionChunk(
          0, prev_blocks * prev_features,
          num_features,
          // 'window' words
          prev_features * window,
          // overlapping, but treating each word as a unit
          prev_features,
          LEAKY_RELU, WEIGHT_UPDATE);

    layers.push_back(Network::LayerFromChunks(conv_chunk));

    // For a window size of two, this always reduces the number
    // of words by one.
    prev_blocks = prev_blocks - (window - 1);
    CHECK(prev_blocks >= 1);
    prev_features = num_features;
  }

  // Need both positive and negative outputs, so TANH makes sense.
  Chunk dense_out =
    Network::MakeDenseChunk(1,
                            0, layers.back().num_nodes,
                            TANH,
                            WEIGHT_UPDATE);

  layers.push_back(Network::LayerFromChunks(dense_out));

  CHECK(layers.back().num_nodes == OUTPUT_SIZE);

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizationParams rparams;
  RandomizeNetwork(&rc, net.get(), rparams, 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}

int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 2) <<
    "./sentencelike.exe dir"
    "Notes:\n"
    "  dir must exist. Resumes training if the dir\n"
    "    contains a model file.\n";

  const string dir = argv[1];

  cl = new CL;

  LoadData();

  const string model_file = Util::dirplus(dir, MODEL_NAME);

  std::unique_ptr<Network> net(Network::ReadFromFile(model_file));

  if (net.get() == nullptr) {
    net = NewSentencelikeNetwork();
    CHECK(net.get() != nullptr);
    net->SaveToFile(model_file);
    printf("Wrote to %s\n", model_file.c_str());
  }

  net->StructuralCheck();
  net->NaNCheck(model_file);

  TrainParams tparams = DefaultParams();
  // tparams.update_config.base_learning_rate = 0.01f;
  // tparams.update_config.learning_rate_dampening = 13.0f;
  tparams.update_config.base_learning_rate = (1.0f / 4096.0f);
  tparams.update_config.learning_rate_dampening = 0.25f;
  tparams.update_config.adam_epsilon = 1.0e-5;

  Train(dir, net.get(), tparams);

  printf("OK\n");
  return 0;
}

