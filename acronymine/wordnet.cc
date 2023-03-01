
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

void WordNet::LoadBareFile(const unordered_set<string> &gamut,
                           const string &filename,
                           uint32_t p) {
  vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    if (line.empty() || Util::StartsWith(line, "#"))
      continue;
    line = Util::NormalizeWhitespace(line);
    line = Util::lcase(line);

    vector<string> tokens = Util::Tokens(line,
                                         [](char c) { return c == ' '; });
    CHECK(tokens.size() == 1) << "Expected one word per line in "
                              << filename << ": " << line;
    if (gamut.empty() || gamut.contains(line)) {
      props[line] |= p;
    }
  }
}


void WordNet::LoadExceptions(const unordered_set<string> &gamut,
                             const string &filename,
                             // e.g. for nouns, prop_var is the plural.
                             // in the file, we have "var base".
                             uint32_t prop_base, uint32_t prop_var,
                             unordered_set<string> *exc) {
  vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    if (line.empty() || Util::StartsWith(line, "  "))
      continue;
    line = Util::NormalizeWhitespace(line);
    line = Util::lcase(line);
    vector<string> tokens = Util::Tokens(line,
                                         [](char c) { return c == ' '; });
    CHECK(tokens.size() == 2);
    const string &word_var = tokens[0];
    const string &word_base = tokens[1];

    // Save to the exceptions set even if it's not in the gamut,
    // since we don't want a word that IS in the gamut to (incorrectly)
    // stem to this.
    (*exc).insert(word_var);
    all_exc.insert(word_var);
    (*exc).insert(word_base);
    all_exc.insert(word_base);

    if (gamut.empty() || gamut.contains(word_var)) {
      props[word_var] |= prop_var;
    }

    if (gamut.empty() || gamut.contains(word_base)) {
      props[word_base] |= prop_base;
    }
  }
}

using MorphTable =
    initializer_list<tuple<const char *, const char *, uint32_t>>;
static constexpr MorphTable MORPH_NOUNS = {
  {"s", "", WordNet::PLURAL},
  {"ses", "s", WordNet::PLURAL},
  {"ves", "f", WordNet::PLURAL},
  {"xes", "x", WordNet::PLURAL},
  {"zes", "z", WordNet::PLURAL},
  {"ches", "ch", WordNet::PLURAL},
  {"shes", "sh", WordNet::PLURAL},
  {"men", "man", WordNet::PLURAL},
  {"ies", "y", WordNet::PLURAL},
};

static constexpr MorphTable MORPH_VERBS = {
  {"s", "", WordNet::PRESENT},
  {"ies", "y", WordNet::PRESENT},
  {"es", "e", WordNet::PRESENT},
  {"es", "", WordNet::PRESENT},
  {"ed", "e", WordNet::PAST},
  {"ed", "", WordNet::PAST},
  {"ing", "e", WordNet::PROGRESSIVE},
  {"ing", "", WordNet::PROGRESSIVE},
};

static constexpr MorphTable MORPH_ADJS = {
  {"er", "", 0},
  {"est", "", 0},
  {"er", "e", 0},
  {"est", "e", 0},
};

static unordered_map<string, uint32_t> ApplyMorph(const string &form,
                                                  const MorphTable &table) {
  unordered_map<string, uint32_t> out;
  for (const auto &[suffix, repl, props] : table) {
    string f = form;
    if (Util::TryStripSuffix(suffix, &f)) {
      out[f + repl] |= props;
    }
  }
  return out;
}

// This is using the "morphy" algorithm from WordNet, although there
// does not appear to be any reason to keep applying the transformations,
// so I didn't do that.
void WordNet::AddStemmed(const unordered_set<string> &gamut) {
  for (const string &form : gamut) {

    if (all_exc.contains(form)) continue;

    unordered_map<string, uint32_t> nouns = ApplyMorph(form, MORPH_NOUNS);
    unordered_map<string, uint32_t> verbs = ApplyMorph(form, MORPH_VERBS);
    unordered_map<string, uint32_t> adjs = ApplyMorph(form, MORPH_ADJS);

    /*
    if (form == "including") {
      printf("including:\n");
      for (const auto &[w, p] : nouns)
        printf("  %s %d\n", w.c_str(), p);
      for (const auto &[w, p] : verbs)
        printf("  %s %d\n", w.c_str(), p);
      for (const auto &[w, p] : adjs)
        printf("  %s %d\n", w.c_str(), p);
    }
    */

    for (const auto &[n, p] : nouns) {
      if (n.size() > 2 && GetProps(n) & NOUN) {
        props[form] |= NOUN | p;
      }
    }

    for (const auto &[v, p] : verbs) {
      if (v.size() > 2 && GetProps(v) & VERB) {
        props[form] |= VERB | p;
      }
    }

    for (const auto &[a, p] : adjs) {
      if (a.size() > 2 && GetProps(a) & ADJ) {
        props[form] |= ADJ | p;
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

  // My own files are just lists of words.
  wn->LoadBareFile(gamut, Util::dirplus(dir, "names.txt"), NAME);


  // Insert words from the exceptions list.
  // This also populates the internal exception list to avoid during
  // stemming.

  // TODO: Exceptional verbs don't get past/present/progressive tags.
  wn->LoadExceptions(gamut, Util::dirplus(dir, "verb.exc"),
                     VERB, VERB, &wn->verb_exc);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "noun.exc"),
                     NOUN, NOUN | PLURAL, &wn->noun_exc);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "adj.exc"),
                     ADJ, ADJ, &wn->adj_exc);
  wn->LoadExceptions(gamut, Util::dirplus(dir, "adv.exc"),
                     ADV, ADV, &wn->adv_exc);

  wn->AddPrepositions(gamut);
  wn->AddPronouns(gamut);
  wn->AddDeterminers(gamut);
  wn->AddConjunctions(gamut);
  wn->AddCommon(gamut);

  wn->AddStemmed(gamut);

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
        "upon", "versus", "via", "with", "within", "without", "thee",
        "unto"}) {
    if (gamut.empty() || gamut.contains(w)) {
      props[w] |= PREP;
    }
  }

}

void WordNet::AddDeterminers(const unordered_set<string> &gamut) {
  for (const string w : {"the", "a", "an",
        "this", "that", "these", "those",
        "my", "your", "his", "her", "its", "our", "their", "thy",
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
        "these", "those", "thy", "thine",
        "my", "your",
        "mine", "yours", "his", "hers", "ours", "yours", "theirs",
        "who", "whom", "what", "which", "whose",
        "myself", "yourself", "himself", "herself", "itself",
        "ourselves", "yourselves", "themselves", "oneself",
        "eachother", "whoever", "whomever",
        "another", "anybody", "anyone", "anything",
        "each", "either", "enough",
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
        "although", "because", "since", "unless", "if", "whereas",
        "whereby", "albeit", }) {
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
  // "Modal verbs"
  AddIf("would", VERB);
  AddIf("should", VERB);
  AddIf("shouldnt", VERB);
  AddIf("could", VERB);
  AddIf("cannot", VERB);
  AddIf("shall", VERB);
  AddIf("breastfeed", VERB);
  AddIf("doing", VERB | PROGRESSIVE);
  AddIf("showcase", VERB | NOUN);

  AddIf("where", CONJ | ADV);
  AddIf("wherein", CONJ | ADV);

  AddIf("respective", ADJ);
  AddIf("previous", ADJ);
  AddIf("uncredited", ADJ);
  AddIf("eventual", ADJ);
  AddIf("longtime", ADJ);
  AddIf("turbo", ADJ);
  AddIf("aforementioned", ADJ);
  AddIf("inline", ADJ);
  AddIf("approx", ADJ);
  AddIf("akin", ADJ);
  AddIf("projective", ADJ);
  AddIf("comedic", ADJ);
  AddIf("redesignated", ADJ | VERB | PAST);
  AddIf("mitochondrial", ADJ);
  AddIf("intercity", ADJ);
  AddIf("programmable", ADJ);
  AddIf("midtown", ADJ | NOUN);
  AddIf("oceanographic", ADJ);
  AddIf("squamous", ADJ);
  AddIf("dimensionless", ADJ);
  AddIf("epigenetic", ADJ);
  AddIf("sortable", ADJ);
  AddIf("unsortable", ADJ);
  AddIf("lone", ADJ);
  AddIf("engined", ADJ);
  AddIf("outlying", ADJ);
  AddIf("virtual", ADJ);
  AddIf("hyper", ADJ | ADV);
  AddIf("aforesaid", ADJ);
  AddIf("hyperbaric", ADJ);
  AddIf("fretless", ADJ);
  AddIf("interagency", ADJ);
  AddIf("admin", ADJ | NOUN);
  AddIf("dockland", NOUN);
  AddIf("nitro", ADJ | NOUN);
  AddIf("historiographical", ADJ);
  AddIf("piecewise", ADJ);
  AddIf("mountaintop", ADJ | NOUN);
  AddIf("nondeterministic", ADJ);
  AddIf("whitespace", ADJ);
  AddIf("treatable", ADJ);

  AddIf("collaboratively", ADV);
  AddIf("telepathically", ADV);

  AddIf("gif", NOUN);
  AddIf("bio", NOUN);
  AddIf("ads", NOUN | PLURAL);
  AddIf("woman", NOUN);
  AddIf("women", NOUN | PLURAL);
  AddIf("equivalently", ADV);
  AddIf("miniseries", NOUN);
  AddIf("filmography", NOUN);
  AddIf("remix", NOUN | VERB);
  AddIf("voivodeship", NOUN);
  AddIf("superhero", NOUN);
  AddIf("statehood", NOUN);
  AddIf("governorate", NOUN);
  AddIf("storytelling", NOUN);
  AddIf("stats", NOUN | PLURAL);
  AddIf("urbanism", NOUN);
  AddIf("heck", NOUN);
  AddIf("coed", ADJ | NOUN);
  AddIf("katakana", NOUN);
  AddIf("fest", NOUN);
  AddIf("groundwater", NOUN);
  AddIf("dept", NOUN);
  AddIf("duopoly", NOUN);
  AddIf("tritone", NOUN);
  AddIf("discriminant", NOUN);
  AddIf("ejecta", NOUN | PLURAL);
  AddIf("covent", NOUN);
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
  if (p & NAME) out.push_back("NAME");

  if (p & PLURAL) out.push_back("PLURAL");
  if (p & PRESENT) out.push_back("PRESENT");
  if (p & PAST) out.push_back("PAST");
  if (p & PROGRESSIVE) out.push_back("PROGRESSIVE");

  return Util::Join(out, ",");
}
