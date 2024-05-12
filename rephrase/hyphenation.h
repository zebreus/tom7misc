
#ifndef _REPHRASE_HYPHENATION_H
#define _REPHRASE_HYPHENATION_H

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <string_view>

struct Hyphenation {
  Hyphenation(std::string_view database_dir = ".");

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

  // If you want to hyphenate a token like ["actually,"] you first
  // need to strip the leading and trailing punctuation so that
  // you can look up [actually] in the hyphenation dictionary.
  // This conveinece function removes non-letters from the left
  // and right side of the word. This also includes space.
  //
  // If the whole thing is punctuation, returns {"", input, ""}.
  std::tuple<std::string_view, std::string_view, std::string_view>
  static SplitPunctuation(std::string_view word);

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> patterns;
};

#endif
