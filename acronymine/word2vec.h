
#ifndef _WORD2VEC_H
#define _WORD2VEC_H

#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "base/logging.h"

struct Word2Vec {

  // Loads the binary file format, e.g.
  // GoogleNews-vectors-negative300.bin
  // If the second argument is non-empty, we only load the
  // strings in that set.
  static Word2Vec *Load(const std::string &filename,
                        const std::unordered_set<std::string> &gamut);

  int Size() const { return size; }
  int NumWords() const { return (int)vocab.size(); }

  // returns -1 if not present.
  int GetWord(const std::string word) const {
    auto it = index.find(word);
    if (it == index.end()) return -1;
    return it->second;
  }

  const std::string &WordString(int idx) const {
    return vocab[idx];
  }

  std::vector<float> Vec(int word) const {
    std::vector<float> ret(size);
    for (int i = 0; i < size; i++)
      ret[i] = values[word * size + i];
    return ret;
  }

  static void Norm(std::vector<float> *v) {
    float len = 0.0f;
    for (float f : *v) len += f * f;
    float inv_len = 1.0f / sqrtf(len);
    for (float &f : *v) f *= inv_len;
  }

  // Cosine distance between two normalized vectors of the same size.
  static float Similarity(const std::vector<float> &vec1,
                          const std::vector<float> &vec2) {
    CHECK(vec1.size() == vec2.size());
    float sum = 0.0f;
    for (int i = 0; i < vec1.size(); i++) {
      sum += vec1[i] * vec2[i];
    }
    // inputs are normalized, so denominator is 1.
    return sum;
  }

  float Similarity(const std::vector<float> &vec1, int word2) const {
    CHECK(vec1.size() == size);
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
      sum += vec1[i] * values[word2 * size + i];
    }
    return sum;
  }

  // Cosine distance, which is always in [-1, 1].
  // word1 and word2 as indices, which must be in bounds.
  float Similarity(int word1, int word2) const {
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
      sum += values[word1 * size + i] * values[word2 * size + i];
    }
    return sum;
  }

  // Slow, O(n) lookup of the word with the highest cosine similarity.
  std::pair<int, float> MostSimilarWord(
      const std::vector<float> &vec1) const {
    float besta = -1.1;
    int bestw = 0;
    for (int w = 0; w < vocab.size(); w++) {
      const float a = Similarity(vec1, w);
      if (a > besta) {
        besta = a;
        bestw = w;
      }
    }
    return make_pair(bestw, besta);
  }

private:
  // Convert between word and its arbitrary index.
  std::vector<std::string> vocab;
  std::unordered_map<std::string, int> index;

  // Dense, word-major. Size |vocab| * size.
  std::vector<float> values;

  // Size of vectors.
  int size = 0;
};

#endif
