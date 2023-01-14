
#ifndef _WORD2VEC_H
#define _WORD2VEC_H

#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "base/logging.h"

struct Word2Vec {

  // This model is missing some simple words like "in" and "and"!
  // They are currently filled in with a hacky approach, using the
  // "fill file."

  // Loads the binary file format, e.g.
  //   GoogleNews-vectors-negative300.bin
  // and using a fill file (if non-empty):
  //   word2vecfill.txt
  // If the third argument is non-empty, we only load the
  // strings in that set.
  static Word2Vec *Load(const std::string &filename,
                        const std::string &fill_file,
                        const std::unordered_set<std::string> &gamut);

  int Size() const { return size; }
  int NumWords() const { return (int)vocab.size(); }

  // returns -1 if not present.
  int GetWord(const std::string &word) const {
    auto it = index.find(word);
    if (it == index.end()) return -1;
    return it->second;
  }

  const std::string &WordString(int idx) const {
    return vocab[idx];
  }

  // Get the normalized vector for a word.
  std::vector<float> NormVec(int word) const {
    std::vector<float> ret(size);
    const float inv_len = inv_lengths[word];
    for (int i = 0; i < size; i++)
      ret[i] = values[word * size + i] * inv_len;
    return ret;
  }

  // Normalize a vector in place.
  static void Norm(std::vector<float> *v) {
    float len = 0.0f;
    for (float f : *v) len += f * f;
    float inv_len = 1.0f / sqrtf(len);
    for (float &f : *v) f *= inv_len;
  }

  static float Length(const std::vector<float> &v) {
    float len = 0.0f;
    for (float f : v) len += f * f;
    return sqrtf(len);
  }

  // Cosine similarity between two normalized vectors of the same size.
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

  // Cosine similarity between a normalized vector and the given word.
  float Similarity(const std::vector<float> &vec1, int word2) const {
    CHECK(vec1.size() == size);
    float sum = 0.0f;
    const float inv_len2 = inv_lengths[word2];
    for (int i = 0; i < size; i++) {
      sum += vec1[i] * values[word2 * size + i];
    }
    // Factoring out the inverse length, which would scale each
    // element of the sum.
    return sum * inv_len2;
  }

  // Cosine distance, which is always in [-1, 1].
  // word1 and word2 as indices, which must be in bounds.
  float Similarity(int word1, int word2) const {
    float sum = 0.0f;
    const float inv_len1 = inv_lengths[word1];
    const float inv_len2 = inv_lengths[word2];
    for (int i = 0; i < size; i++) {
      sum += values[word1 * size + i] * values[word2 * size + i];
    }
    // Factoring out the inverse lengths, which would scale each
    // element.
    return sum * inv_len1 * inv_len2;
  }

  // Slow, O(n) lookup of the word with the highest cosine similarity.
  // Requires a normalized vector.
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
  // Not normalized.
  std::vector<float> values;
  // Multiplier to norm a vector. Size |vocab|.
  std::vector<float> inv_lengths;

  // Size of vectors.
  int size = 0;
};

#endif
