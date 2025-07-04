
#include "sentencelike.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "freq.h"
#include "gtl/top_n.h"
#include "network-gpu.h"
#include "network.h"
#include "opencl/clutil.h"
#include "threadutil.h"
#include "timer.h"
#include "word2vec.h"
#include "wordnet.h"

void SentenceLike::Init(CL *cl_in,
                        Word2Vec *w2v_in, Freq *freq_in, WordNet *wordnet_in,
                        int max_words_per_char) {
  cl = cl_in;

  CHECK(cl != nullptr);
  net_gpu = std::make_unique<NetworkGPU>(cl, net.get());
  forward_cl = std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  printf("Compiled SentenceLike model.\n");

  w2v = w2v_in;
  CHECK(w2v->Size() == VEC_SIZE);

  freq = freq_in;
  wordnet = wordnet_in;

  // Prep list of words to try:
  for (const std::string &s : freq->SortedWords()) {
    CHECK(!s.empty());
    const char c = s[0];
    // Must be in word2vec
    int w = w2v->GetWord(s);
    if (w < 0)
      continue;

    // And also in wordnet
    uint32_t props = wordnet->GetProps(s);
    if (props == 0)
      continue;

    // PERF? could pre-fill batches here
    if (max_words_per_char >= 0 &&
        words_to_try[c].size() >= max_words_per_char)
      break;

    words_to_try[c].push_back(w);
  }
  printf("Initialized SentenceLike.\n");
}

// [  ][  ][  ][  ][  ][  ][  ][  ][  ]

struct GreaterPair {
  bool operator ()(const std::pair<uint32_t, float> &a,
                   const std::pair<uint32_t, float> &b) const {
      CHECK(std::isfinite(a.second));
      CHECK(std::isfinite(b.second));
      if (a.second > b.second) return true;
      if (b.second > a.second) return false;
      return a.first < b.first;
    }
};

std::vector<std::pair<uint32_t, float>>
SentenceLike::PredictOne(const std::vector<uint32_t> &phrase,
                         char c,
                         int slot_idx,
                         int num_best) {
  CHECK(w2v != nullptr);
  printf("Take mutex %d\n", slot_idx);
  MutexLock ml(&mu);
  printf("Inside %d\n", slot_idx);

  // Uninitialized training examples on GPU.
  // PERF: Only need the stimulations to be allocated here.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(BATCH_SIZE, cl, *net));

  gtl::TopN<std::pair<uint32_t, float>, GreaterPair> best(num_best);

  std::vector<float> inputs;
  inputs.reserve(BATCH_SIZE);
  std::vector<uint32_t> ws;
  ws.reserve(BATCH_SIZE);
  double fwd_time = 0.0;
  int passes = 0;
  auto ProcessBatch = [&]() {
      const int num = ws.size();
      if (num == 0) return;

      // Need a full batch, even if we're not using all of the width.
      int padding = BATCH_SIZE * INPUT_SIZE - inputs.size();
      for (int i = 0; i < padding; i++) inputs.push_back(0.0f);

      training->LoadInputs(inputs);
      CHECK(num <= BATCH_SIZE);
      // PERF if the last batch is full, we could maybe run
      // it faster. Here we just run it on all the inputs
      // (potentially uninitialized/stale) and ignore it if
      // it's past the end num.
      Timer fwd_timer;
      for (int src_layer = 0;
           src_layer < net->layers.size() - 1;
           src_layer++) {
        forward_cl->RunForward(training.get(), src_layer);
      }
      fwd_time += fwd_timer.Seconds();;
      // printf("Took %.5fs\n", fwd_timer.Seconds());

      std::vector<float> outputs;
      outputs.resize(BATCH_SIZE * OUTPUT_SIZE);
      training->ExportOutputs(&outputs);
      for (int i = 0; i < num; i++) {
        float score = outputs[i];
        // printf("%d:%.3f\n", ws[i], score);
        best.push(std::make_pair(ws[i], score));
      }
      passes++;
      printf("%d passes ok\n", passes);
    };

  // Already checked that all the words here are in w2v, freq, wordnet.
  auto PushWord = [&](uint32_t w) {
      const std::string &word = w2v->WordString(w);
      for (float f : w2v->NormVec(w)) {
        inputs.push_back(f);
      }
      // frequency
      inputs.push_back(freq->NormalizedFreq(word));
      // wordnet props
      uint32_t props = wordnet->GetProps(word);
      for (int i = 0; i < WORDNET_PROPS; i++) {
        inputs.push_back((props & (1 << i)) ? 1.0f : 0.0f);
      }
    };

  const int num_words = words_to_try[c].size(); // std::min(num_best, (int)words_to_try[c].size());

  printf("There are %d num_words\n", num_words);

  for (int idx = 0; idx < num_words; idx++) {
    const uint32_t w = words_to_try[c][idx];
    ws.push_back(w);

    // Push the whole phrase, but with the word filled into the slot.
    for (int j = 0; j < PHRASE_SIZE; j++) {
      uint32_t ww = slot_idx == j ? w : phrase[j];
      PushWord(ww);
    }

    if (ws.size() == BATCH_SIZE) {
      ProcessBatch();
      ws.clear();
      inputs.clear();
    }
  }

  // Incomplete batch, if any.
  ProcessBatch();

  printf("Extract best:\n");

  std::unique_ptr<std::vector<std::pair<uint32_t, float>>> b(
      best.Extract());
  printf("Extracted %d..\n", (int)b->size());
  CHECK(b.get() != nullptr);
  if (b->empty()) {
    printf("Empty!\n");
  } else {
    printf("[%d, %.3f]\n", (*b)[0].first, (*b)[0].second);
  }
  printf("NONCE\n");

  printf("%.3fs sec in %d passes\n", fwd_time, passes);
  std::vector<std::pair<uint32_t, float>> ret = *b;
  printf("Return it..\n");
  return ret;
}
