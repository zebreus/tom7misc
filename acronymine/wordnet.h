
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
  //
  // It is recommended to pass a good wordlist as the gamut:
  // If the gamut is empty, we don't do any stemming (e.g. plurals);
  // only the words literally in wordnet are included. (This does
  // include some words that have exceptional forms, though.)
  static WordNet *Load(const std::string &dir,
                       const std::unordered_set<std::string> &gamut);

  int NumWords() const { return props.size(); }

  // Properties of a word.
  static constexpr uint32_t ADJ = ((uint32_t)1 << 0);
  static constexpr uint32_t ADV = ((uint32_t)1 << 1);
  static constexpr uint32_t NOUN = ((uint32_t)1 << 2);
  static constexpr uint32_t VERB = ((uint32_t)1 << 3);
  static constexpr uint32_t PREP = ((uint32_t)1 << 4);
  static constexpr uint32_t DET = ((uint32_t)1 << 5);
  static constexpr uint32_t PRO = ((uint32_t)1 << 6);
  static constexpr uint32_t CONJ = ((uint32_t)1 << 7);
  static constexpr uint32_t NAME = ((uint32_t)1 << 8);

  // Result of stemming or exceptions list.
  // Nouns
  static constexpr uint32_t PLURAL = ((uint32_t)1 << 9);
  // Verbs
  static constexpr uint32_t PRESENT = ((uint32_t)1 << 10);
  static constexpr uint32_t PAST = ((uint32_t)1 << 11);
  static constexpr uint32_t PROGRESSIVE = ((uint32_t)1 << 12);

  static constexpr int NUM_PROPS = 13;

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
  void LoadBareFile(const std::unordered_set<std::string> &gamut,
                    const std::string &filename,
                    uint32_t p);
  void LoadExceptions(const std::unordered_set<std::string> &gamut,
                      const std::string &filename,
                      uint32_t prop_base, uint32_t prop_var,
                      std::unordered_set<std::string> *exc);

  // builtins
  void AddPrepositions(const std::unordered_set<std::string> &gamut);
  void AddDeterminers(const std::unordered_set<std::string> &gamut);
  void AddPronouns(const std::unordered_set<std::string> &gamut);
  void AddConjunctions(const std::unordered_set<std::string> &gamut);

  void AddCommon(const std::unordered_set<std::string> &gamut);

  void AddStemmed(const std::unordered_set<std::string> &gamut);

  std::unordered_map<std::string, uint32_t> props;

  // Base words that have exceptions, for each grammatical class.
  // We don't stem to these.
  std::unordered_set<std::string> verb_exc;
  std::unordered_set<std::string> noun_exc;
  std::unordered_set<std::string> adj_exc;
  std::unordered_set<std::string> adv_exc;

  std::unordered_set<std::string> all_exc;
};

#endif
