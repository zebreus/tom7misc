
#ifndef _SENTENCELIKE_H
#define _SENTENCELIKE_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>

#include "base/logging.h"

#include "threadutil.h"
#include "network.h"
#include "network-gpu.h"
#include "clutil.h"

#include "freq.h"
#include "word2vec.h"
#include "wordnet.h"

struct SentenceLike {
  static constexpr int BATCH_SIZE = 1024;
  explicit SentenceLike(const std::string &model_file)
    : net(Network::ReadFromFile(model_file)) {
    CHECK(net.get() != nullptr);
    net->StructuralCheck();
    net->NaNCheck(model_file);
  }

  static constexpr int VEC_SIZE = 300;
  static constexpr int WORDNET_PROPS = 13;
  static constexpr int PHRASE_SIZE = 8;

  // w2v vector, normalized frequency, wordnet props
  static constexpr int ONE_WORD_SIZE = (VEC_SIZE + 1 + WORDNET_PROPS);

  static constexpr int INPUT_SIZE = ONE_WORD_SIZE * PHRASE_SIZE;
  static constexpr int OUTPUT_SIZE = 1;

  // Not owned.
  CL *cl = nullptr;
  Word2Vec *w2v = nullptr;
  Freq *freq = nullptr;
  WordNet *wordnet = nullptr;
  std::mutex mu;

  std::unordered_map<char, std::vector<uint32_t>> words_to_try;

  void Init(CL *cl,
            Word2Vec *w2v_in, Freq *freq_in, WordNet *wordnet_in,
            int max_words_per_char);

  // Given a phrase of the target size and a slot to fill,
  // return the num_best highest scoring words for that slot
  // that start with the letter c.
  // The word id in the slot is ignored.
  // The returned vector is in descending order by score.
  std::vector<std::pair<uint32_t, float>>
  PredictOne(const std::vector<uint32_t> &phrase,
             char c,
             int slot_idx,
             int num_best);

  std::unique_ptr<Network> net;
  std::unique_ptr<NetworkGPU> net_gpu;
  std::unique_ptr<ForwardLayerCL> forward_cl;
  std::unique_ptr<TrainingRoundGPU> training;
};

#endif
