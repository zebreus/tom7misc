
#ifndef _REPHRASE_HYPHENATION_H
#define _REPHRASE_HYPHENATION_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

struct Hyphenation {
  Hyphenation();

  // Hyphenate a word. Ignores case (ASCII). The word should have
  // punctuation removed first.
  //
  // The left- and right hyphen minima give the shortest prefix
  // and suffix that can be hyphenated. It is not recommended
  // to reduce these for English, as it will produce unnatural
  // results (e.g. king hyphenated as "k-ing").
  std::vector<std::string> Hyphenate(std::string_view word,
                                     int lefthyphenmin = 2,
                                     int righthyphenmin = 3);

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> patterns;
};

#endif
