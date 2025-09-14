#include "hyphenation.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"

static constexpr bool VERBOSE = false;

// The hyphenation format:
// See "Word Hy-phen-a-tion by Com-put-er", Liang, 1983
// Most of the document is about how to generate and store the
// hyphenation databases; page 37 has examples of the TeX format.
//
// . at the beginning or end means the beginning or end of the word.
// The numbers are potential hyphenation spots. When multiple
// patterns match, the highest number takes precedence. (If you
// like, you can imagine a 0 everywhere that there isn't a digit.)
// There are multiple numbers because some patterns override
// other patterns, and this is done in alternating allowlists and
// denylists. So, if the final number is ODD, hyphenation is allowed
// there.
//
// Additionally, the rules seem to be designed with a minimum
// hyphenation length in mind. This appears to be 2 characters for
// the shortest prefix, and 3 characters for the shortest suffix.
// Without this, you get hyphenations like "k-ing". These are parameters
// to the hyphenation function.

static constexpr const char *DATABASE =
  "hyph-en-us.tex";

inline static bool IsDigit(char c) {
  return c >= '0' && c <= '9';
}

static std::pair<std::string, std::vector<uint8_t>> Decode(
    std::string s) {
  CHECK(!s.empty());
  // Implicit.
  if (!IsDigit(s.back())) s.push_back('0');

  std::string key;
  std::vector<uint8_t> code;
  for (int i = 0; i < (int)s.size(); i++) {
    // Repeatedly get an optional digit, then a character.
    char c = s[i];
    if (IsDigit(c)) {
      code.push_back(c - '0');
      i++;
      if (i == (int)s.size())
        break;
    } else {
      code.push_back(0);
    }

    char a = s[i];
    CHECK(a == '.' || (a >= 'a' && a <= 'z')) << "Not hard to support "
      "other character sets here, but we're not expecting it. " <<
      "\nGot char " << a << " in pattern: " << s;
    key.push_back(a);
  }

  // One larger, because we get a code at the beginning and end.
  CHECK(code.size() == key.size() + 1);
  return std::make_pair(std::move(key), std::move(code));
}

Hyphenation::Hyphenation(std::string_view database_dir) {
  std::string db_file = Util::DirPlus(database_dir, DATABASE);
  std::vector<std::string> lines =
    Util::NormalizeLines(Util::ReadFileToLines(db_file));

  enum Section {
    NONE,
    PATTERNS,
    HYPHENATION,
  };

  int num_patterns = 0;
  Section section = NONE;
  for (std::string &line : lines) {
    if (line.empty()) continue;
    if (line[0] == '%') continue;

    if (Util::TryStripPrefix("\\patterns{", &line)) {
      CHECK(section == NONE) << "Unexpected hyphenation database format.";
      section = PATTERNS;
    } else if (Util::TryStripPrefix("}", &line)) {
      CHECK(section == PATTERNS || section == HYPHENATION)
        << "Unexpected hyphenation database format.";
      section = NONE;
    } else if (Util::TryStripPrefix("\\hyphenation{", &line)) {
      CHECK(section == NONE) << "Unexpected hyphenation database format.";
      section = HYPHENATION;
    } else {
      CHECK(section != NONE) << "Unexpected command (?) outside of section "
        "I understand in hyphenation database: " << line;
      if (section == PATTERNS) {
        const auto &[key, code] = Decode(line);
        patterns[key] = code;
      } else {
        // An exception like "ta-ble" is equivalent to ".8t8a9b8l8e8."
        // (Maximum priority hyphens or non-hyphens at each slot.)
        std::string pat = ".";
        for (int i = 0; i < (int)line.size(); i++) {
          if (line[i] == '-') {
            pat.push_back('9');
            i++;
          } else {
            pat.push_back('8');
          }
          CHECK(i < (int)line.size()) << "Forced trailing hyphen?";
          pat.push_back(line[i]);
        }
        pat += "8.";

        const auto &[key, code] = Decode(pat);
        patterns[key] = code;
      }

      num_patterns++;
    }
  }

  if (VERBOSE) {
    Print("Loaded {} hyphenation patterns.\n", num_patterns);
  }
}

std::vector<std::string> Hyphenation::Hyphenate(std::string_view word,
                                                int lefthyphenmin,
                                                int righthyphenmin) {
  std::string lword = "." + Util::lcase(std::string(word)) + ".";

  std::vector<uint8_t> values(lword.size() + 1, 0);

  // PERF: Use a trie or state machine to match patterns.
  for (int start = 0; start < (int)lword.size(); start++) {
    for (int end = start + 1; end <= (int)lword.size(); end++) {
      // std::string_view part =
      // std::string_view(lword).substr(start, end - start);
      std::string part = lword.substr(start, end - start);
      const auto it = patterns.find(part);
      if (it != patterns.end()) {
        const std::vector<uint8_t> &code = it->second;
        if (VERBOSE) {
          Print("Matched " AYELLOW("{}") " with code ", part);
          for (int i = 0; i < (int)code.size(); i++) {
            Print("{} ", code[i]);
          }
          Print("\n");
        }
        for (int i = 0; i < (int)code.size(); i++) {
          int outpos = start + i;
          CHECK(outpos >= 0 && outpos < (int)values.size());
          values[outpos] = std::max(values[outpos], code[i]);
        }
      }
    }
  }

  if (VERBOSE) {
    Print("For word [{}]:\n", std::string(word));
    for (int i = 0; i < (int)lword.size(); i++) {
      Print(" {:c}", lword[i]);
    }
    Print("\n");
    for (int i = 0; i < (int)values.size(); i++) {
      if (values[i] & 1) {
        Print(AGREEN("{:c}") " ", '0' + values[i]);
      } else {
        Print("{:c} ", '0' + values[i]);
      }
    }
    Print("\n");
  }

  CHECK(values[0] == 0 && values.back() == 0) << "The patterns should "
    "not allow hyphens outside the beginning and end-of-word sentinels!";

  // Remove hyphenation points that are too close to the ends of words.
  for (int i = 0; i <= lefthyphenmin && i < (int)values.size(); i++) {
    values[i] = 0;
  }

  // Need to deal with the edge conditions, plus the '.'. By example:
  // When there are 10 values and righthyphenmin is 3, we want to start
  // at index 6.
  const int start_idx = (int)values.size() - (righthyphenmin + 1);

  for (int i = std::max(0, start_idx); i < (int)values.size(); i++) {
    values[i] = 0;
  }

  if (VERBOSE) {
    for (int i = 0; i < (int)values.size(); i++) {
      if (values[i] & 1) {
        Print(AGREEN("{:c}") " ", '0' + values[i]);
      } else {
        Print("{:c} ", '0' + values[i]);
      }
    }
    Print("\n");
  }

  std::vector<std::string> out;

  int cur_start = 1;
  for (int i = 1; i < (int)values.size() - 1; i++) {
    if (values[i] & 1) {
      if (cur_start < i) {
        out.push_back(
            std::string(word.substr(cur_start - 1, i - cur_start)));
        cur_start = i;
      }
    }
  }

  int last = values.size() - 1;
  if (cur_start < last) {
    out.push_back(
        std::string(word.substr(cur_start - 1, last - cur_start)));
  }

  return out;
}

// We do want to handle unicode like "em dash" and "fancy quotes"
// touching the words.
//
// Since we only hyphenate ASCII, it's simplest to allowlist the
// characters that count as part of a word, and treat everything
// else as punctuation. This could have false positives for unicode
// words that contain ASCII. To do this correctly, I think we need
// to use the unicode props tables.
static bool IsWordConstituent(char c) {
  return (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') ||
    (c >= '0' && c <= '9');
}

std::tuple<std::string_view, std::string_view, std::string_view>
Hyphenation::SplitPunctuation(std::string_view word_in) {
  std::string_view word = word_in;
  size_t prefix_size = 0;
  while (!word.empty() && !IsWordConstituent(word[0])) {
    prefix_size++;
    word.remove_prefix(1);
  }

  if (word.empty()) return std::make_tuple(std::string_view(),
                                           word_in,
                                           std::string_view());

  size_t suffix_size = 0;
  while (!word.empty() && !IsWordConstituent(word.back())) {
    suffix_size++;
    word.remove_suffix(1);
  }

  CHECK(!word.empty()) << "We should have found these characters "
    "in the prefix process above, then.";

  CHECK(prefix_size + word.size() + suffix_size == word_in.size());

  std::string_view prefix =
    word_in.substr(0, prefix_size);
  std::string_view suffix =
    word_in.substr(word_in.size() - suffix_size, suffix_size);
  return std::make_tuple(prefix, word, suffix);
}
