#ifndef __PLUGINVERT_DIRECT_WORD_PROBLEM_H
#define __PLUGINVERT_DIRECT_WORD_PROBLEM_H

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "util.h"
#include "base/logging.h"

// make-wordlist.exe
static constexpr const char *WORDLIST = "wordlist.txt";
// Can get this from the wordlist, but it is useful as a compile-time
// constant.
// static constexpr int WORDLIST_SIZE = 65536;
// static constexpr int WORDLIST_SIZE = 2048;
static constexpr int WORDLIST_SIZE = 1024;

static constexpr const char *MODEL_NAME = "direct-words.val";

// This network predicts a middle word given some words before
// and some words after.
static constexpr int WORDS_BEFORE = 3, WORDS_AFTER = 2;
static constexpr int NUM_WORDS = WORDS_BEFORE + WORDS_AFTER + 1;

struct Wordlist {
  Wordlist() : words(Util::ReadFileToLines(WORDLIST)) {
    CHECK(!words.empty()) << WORDLIST;
    CHECK(words.size() >= WORDLIST_SIZE);
    words.resize(WORDLIST_SIZE);
    for (int i = 0; i < words.size(); i++) {
      const std::string &w = words[i];
      CHECK(word_to_id.find(w) == word_to_id.end()) << w;
      word_to_id[w] = i;
    }
    CHECK(word_to_id.size() == WORDLIST_SIZE);
  }

  static constexpr int NumWords() { return WORDLIST_SIZE; }

  const std::string &GetWord(int i) const {
    CHECK(i >= 0 && i < words.size());
    return words[i];
  }

  // Returns -1 for words not in list.
  // Otherwise id is in [0, NumWords()).
  int GetId(const std::string &s) const {
    auto it = word_to_id.find(s);
    if (it == word_to_id.end()) return -1;
    return it->second;
  }

private:
  std::vector<std::string> words;
  std::unordered_map<std::string, int> word_to_id;
};

struct DirectWordProblem {
  static constexpr int INPUT_SIZE = (WORDS_BEFORE + WORDS_AFTER) *
    WORDLIST_SIZE;
  static constexpr int OUTPUT_SIZE = (WORDS_BEFORE + WORDS_AFTER + 1) *
    WORDLIST_SIZE;

  static std::vector<float> LoadInput(const std::vector<int> &word_ids) {
    CHECK(word_ids.size() == WORDS_BEFORE + WORDS_AFTER);
    std::vector<float> inputs(INPUT_SIZE, 0.0f);

    for (int w = 0; w < (WORDS_BEFORE + WORDS_AFTER); w++) {
      int hot = word_ids[w];
      inputs[w * WORDLIST_SIZE + hot] = 1.0f;
    }

    return inputs;
  }

  // Returns word ids. Output can contain -1 if no predictions were positive.
  static std::vector<int> DecodeOutput(const std::vector<float> &v) {
    constexpr int NUM = WORDS_BEFORE + WORDS_AFTER + 1;
    CHECK(v.size() == NUM * WORDLIST_SIZE);
    std::vector<int> ret;
    ret.reserve(NUM);
    for (int w = 0; w < NUM; w++) {

      int bestid = -1;
      float bestscore = 0.0f;
      for (int i = 0; i < WORDLIST_SIZE; i++) {
        float s = v[w * WORDLIST_SIZE + i];
        if (s > bestscore) {
          bestscore = s;
          bestid = i;
        }
      }

      ret.push_back(bestid);
    }
    return ret;
  };


};


#endif
