
#include "wordnet.h"

#include <unordered_set>
#include <unordered_map>
#include <initializer_list>
#include <string>

#include "util.h"
#include "base/logging.h"

using namespace std;

void WordNet::LoadFile(const unordered_set<string> &gamut,
                       const string &filename) {
  vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    if (line.empty() || Util::StartsWith(line, "  "))
      continue;
    line = Util::NormalizeWhitespace(line);
    line = Util::lcase(line);

    // 0        1       2      3      4     5     6    7 ...
    // 00003589 03      n      02     whole 0     unit 0 ... bunch more ...
    //  ^id     ^class  ^pos   ^num   ^ entry 1   ^ entry 2
    //
    // The class could be interesting in the future (see
    // lexnames) but for now we just use the single-letter part-of-speech
    // descriptor.

    vector<string> tokens = Util::Tokens(line,
                                         [](char c) { return c == ' '; });
    if (tokens.size() < 6)
      continue;

    CHECK(tokens[2].size() == 1) <<
      "bad/unknown part of speech: " << tokens[2];
    uint32_t pos = 0;
    switch (tokens[2][0]) {
    case 'n': pos = NOUN; break;
    case 's': // not sure what the distinction is
    case 'a': pos = ADJ; break;
    case 'r': pos = ADV; break;
    case 'v': pos = VERB; break;
    default:
      LOG(FATAL) << "Unknown part of speech in " <<
        filename << " " << tokens[0] << ": " << tokens[2];
    }

    // The count is in hex
    const int num = strtol(tokens[3].c_str(), nullptr, 16);
    CHECK(num > 0) << "Bad num? " << filename << " " <<
      tokens[0] << ": " << tokens[3];
    for (int i = 0; i < num; i++) {
      int idx = 4 + i * 2;
      CHECK(idx + 1 < tokens.size()) <<
        "Num is outside line? id: " << tokens[0] << " num: " << num;
      string word = tokens[idx];
      if (gamut.empty() || gamut.contains(word)) {
        props[word] |= pos;
      }
    }
  }
}

void WordNet::LoadExceptions(const unordered_set<string> &gamut,
                             const string &filename,
                             uint32_t prop) {
  vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    if (line.empty() || Util::StartsWith(line, "  "))
      continue;
    line = Util::NormalizeWhitespace(line);
    line = Util::lcase(line);
    vector<string> tokens = Util::Tokens(line,
                                         [](char c) { return c == ' '; });
    for (const string &word : tokens) {
      if (gamut.empty() || gamut.contains(word)) {
        props[word] |= prop;
      }
    }
  }
}

WordNet *WordNet::Load(const string &dir,
                       const unordered_set<string> &gamut) {
  WordNet *wn = new WordNet;
  wn->LoadFile(gamut, Util::dirplus(dir, "data.adj"));
  wn->LoadFile(gamut, Util::dirplus(dir, "data.adv"));
  wn->LoadFile(gamut, Util::dirplus(dir, "data.noun"));
  wn->LoadFile(gamut, Util::dirplus(dir, "data.verb"));

  // TODO: It looks like it's expected that we do some stemming
  // on the words above (maybe if they're not in the exceptions
  // lists?). But we can at least insert words from the exceptions
  // lists:

  wn->LoadExceptions(gamut, Util::dirplus(dir, "verb.exc"), VERB);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "noun.exc"), NOUN);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "adj.exc"), ADJ);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "adv.exc"), ADV);

  wn->AddPrepositions(gamut);
  wn->AddPronouns(gamut);
  wn->AddDeterminers(gamut);
  wn->AddConjunctions(gamut);
  wn->AddCommon(gamut);

  return wn;
}

void WordNet::AddPrepositions(const unordered_set<string> &gamut) {

  for (const string w : {
      "aboard", "about", "above", "across", "after", "against", "along",
        "amid", "among", "anti", "around", "as", "at", "before", "behind",
         "below", "beneath", "beside", "besides", "between", "beyond", "but",
         "by", "concerning", "considering", "despite", "down", "during",
         "except", "excepting", "excluding", "following", "for", "from",
         "in", "inside", "into", "like", "minus", "near", "of", "off",
         "on", "onto", "opposite", "outside", "over", "past", "per", "plus",
         "regarding", "round", "save", "since", "than", "through", "to",
         "toward", "towards", "under", "underneath", "unlike", "until", "up",
        "upon", "versus", "via", "with", "within", "without"}) {
    if (gamut.empty() || gamut.contains(w)) {
      props[w] |= PREP;
    }
  }

}

void WordNet::AddDeterminers(const unordered_set<string> &gamut) {
  for (const string w : {"the", "a", "an",
        "this", "that", "these", "those",
        "my", "your", "his", "her", "its", "our", "their",
        "all", "every", "most", "many", "much", "some", "few",
        "little", "any", "no",
        "zero", "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve", "thirteen",
        "fourteen", "fifteen", "sixteen", "seventeen", "eighteen"
        "nineteen", "twenty", "thirty", "forty", "fifty", "sixty",
        "seventy", "eighty", "ninety",
        "double", "twice",
        "whose", "what", "which",
        }) {
    if (gamut.empty() || gamut.contains(w)) {
      props[w] |= DET;
    }
  }
}

void WordNet::AddPronouns(const unordered_set<string> &gamut) {
  for (const string w : {"i", "me", "you", "he", "him", "she", "her",
        "it", "we", "us", "they", "them",
        "this", "that"
        "these", "those",
        "my", "your",
        "mine", "yours", "his", "hers", "ours", "yours", "theirs",
        "who", "whom", "what", "which", "whose",
        "myself", "yourself", "himself", "herself", "itself",
        "ourselves", "yourselves", "themselves",
        "eachother",
        "another", "anybody", "anyone", "each", "either", "enough",
        "everybody", "everyone", "everything", "less", "little",
        "much", "neither", "nobody", "nothing", "one", "other",
        "somebody", "someone", "something", "you", "both", "few",
        "fewer", "many", "others", "several", "they", "all", "any",
        "more", "most", "none", "some", "such",
        "which", "that"}) {
    if (gamut.empty() || gamut.contains(w)) {
      props[w] |= PRO;
    }
  }
}

void WordNet::AddConjunctions(const unordered_set<string> &gamut) {
  for (const string w : {"and", "but", "or", "nor", "for", "yet", "so",
        "although", "because", "since", "unless", "if"}) {
    if (gamut.empty() || gamut.contains(w)) {
      props[w] |= CONJ;
    }
  }
}

// Some other common words that are missing from the data, usually
// because they have multiple functions.
void WordNet::AddCommon(const unordered_set<string> &gamut) {
  auto AddIf = [this, &gamut](const string &word, uint32_t prop) {
      if (gamut.empty() || gamut.contains(word))
        props[word] |= prop;
    };

  AddIf("when", ADV | CONJ | PRO);
  AddIf("would", VERB);
  AddIf("should", VERB);
  AddIf("could", VERB);

  AddIf("where", CONJ | ADV);

  AddIf("woman", NOUN);
}

std::string WordNet::PropString(uint32_t p) {
  if (p == 0) return "NONE";

  vector<string> out;
  if (p & ADJ) out.push_back("ADJ");
  if (p & ADV) out.push_back("ADV");
  if (p & NOUN) out.push_back("NOUN");
  if (p & VERB) out.push_back("VERB");
  if (p & PREP) out.push_back("PREP");
  if (p & DET) out.push_back("DET");
  if (p & PRO) out.push_back("PRO");
  if (p & CONJ) out.push_back("CONJ");
  return Util::Join(out, ",");
}
