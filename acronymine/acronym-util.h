
#ifndef _ACRONYMY_ACRONYM_UTIL_H
#define _ACRONYMY_ACRONYM_UTIL_H

#include <vector>
#include <string>
#include <array>
#include <cstdint>

#include "util.h"
#include "lastn-buffer.h"
#include "word2vec.h"

struct AcronymUtil {
  using string = std::string;

  // Look up overlapping phrases of length PHRASE_SIZE to get their
  // word2vec ids; reject ones with unknown words; return the rest.
  template<size_t PHRASE_SIZE>
  static std::vector<std::array<uint32_t, PHRASE_SIZE>> GetPhrases(
      const Word2Vec *w2v,
      const std::vector<std::string> &article_words) {
    std::vector<std::array<uint32_t, PHRASE_SIZE>> ret;

    LastNBuffer<int> buffer(PHRASE_SIZE, -1);

    // TODO: We should be smarter about sentence boundaries!

    for (const string &case_word : article_words) {
      const string word = Util::lcase(case_word);
      if (word.empty())
        continue;

      const int idx = w2v->GetWord(word);
      // Deliberately including non-words here.
      buffer.push_back(idx);

      auto AllValid = [](const LastNBuffer<int> &b) {
          for (int i = 0; i < b.size(); i++)
            if (b[i] < 0) return false;
          return true;
        };

      // This includes words not in word2vec, and when
      // the buffer is not yet long enough.
      if (!AllValid(buffer))
        continue;

      // Since it's valid, add to pool.
      ret.resize(ret.size() + 1);
      std::array<uint32_t, PHRASE_SIZE> &phrase = ret.back();
      for (int i = 0; i < buffer.size(); i++) {
        phrase[i] = buffer[i];
      }
    }
    return ret;

  }

};

#endif
