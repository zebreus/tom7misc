
#ifndef _REPHRASE_HYPHENATION_H
#define _REPHRASE_HYPHENATION_H

#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

struct Hyphenation {
  Hyphenation();

  // Hyphenate a word. Ignores case (ASCII). The word should have
  // punctuation removed first.
  std::vector<std::string> Hyphenate(std::string_view word);

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> patterns;
};

#endif
