
#ifndef _WORDNET_H
#define _WORDNET_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

// Unprincipled parsing of some of the data in WordNet. Here
// just the parts of speech.
// (Open English Wordnet 2021; I got the download from NLTK.)

struct WordNet {
  // If gamut is non-empty, then only members of the set are loaded.
  static WordNet *Load(const std::string &dir,
                       const std::unordered_set<std::string> &gamut);

  // Properties of a word.
  static constexpr uint32_t ADJ = ((uint32_t)1 << 0);
  static constexpr uint32_t ADV = ((uint32_t)1 << 1);
  static constexpr uint32_t NOUN = ((uint32_t)1 << 2);
  static constexpr uint32_t VERB = ((uint32_t)1 << 3);
  static constexpr uint32_t PREP = ((uint32_t)1 << 4);
  static constexpr uint32_t DET = ((uint32_t)1 << 5);
  static constexpr uint32_t PRO = ((uint32_t)1 << 6);
  static constexpr uint32_t CONJ = ((uint32_t)1 << 7);
  static constexpr int NUM_PROPS = 8;

  // Get the property bitmask for the word. Returns 0 for unknown
  // words (but also for known words with no properties).
  uint32_t GetProps(const std::string &word) const {
    auto it = props.find(word);
    if (it == props.end()) return 0u;
    return it->second;
  }

  static std::string PropString(uint32_t p);

private:
  void LoadFile(const std::unordered_set<std::string> &gamut,
                const std::string &filename);
  void LoadExceptions(const std::unordered_set<std::string> &gamut,
                      const std::string &filename,
                      uint32_t prop);


  // builtins
  void AddPrepositions(const std::unordered_set<std::string> &gamut);
  void AddDeterminers(const std::unordered_set<std::string> &gamut);
  void AddPronouns(const std::unordered_set<std::string> &gamut);
  void AddConjunctions(const std::unordered_set<std::string> &gamut);

  void AddCommon(const std::unordered_set<std::string> &gamut);

  std::unordered_map<std::string, uint32_t> props;
};

#endif
