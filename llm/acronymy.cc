
#include "llama.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"

#include "llm.h"

using namespace std;

static bool IsAscii(const std::string &s) {
  for (char c : s) {
    if (c < ' ' || c > '~') return false;
  }
  return true;
}

static bool AllSpace(const std::string &s) {
  for (char c : s) {
    if (c != ' ') return false;
  }
  return true;
}

static bool IsAlphabetical(const std::string &s) {
  for (char c : s) {
    if (c == ' ') continue;
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    return false;
  }
  return true;
}

static inline bool ContainsChar(const std::string &s, char t) {
  for (char c : s)
    if (c == t) return true;
  return false;
}

static std::string AnsiTime(double seconds) {
  if (seconds < 1.0) {
    return StringPrintf(AYELLOW("%.2f") "ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "h"
                        AYELLOW("%d") "m"
                        AYELLOW("%02d") "s",
                        ohour, omin, osec);
  }
}

static void EmitTimer(const std::string &name, const Timer &timer) {
  printf(AWHITE("%s") " in %s\n",
         name.c_str(),
         AnsiTime(timer.Seconds()).c_str());
}

int main(int argc, char ** argv) {
  AnsiInit();
  Timer model_timer;

  LLM::Params lparams;
  lparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  lparams.mirostat = 2;

  LLM llm(lparams);
  EmitTimer("Loaded model", model_timer);

  std::vector<std::string> words = {
    "racers",
    "hauling",
    "bottom",
    "meritless",
    "exciting",
    "applying",
    "independent",
    "related",
    "unifying",
    "summarizing",
    "vivacious",
    "offering",
    "occurrences",
    "scrutiny",
    "unlikely",
    "lives",
    "mammals",
    "violence",
    "nonsensical",
    "crazily",
  };

# define WORD_PREFIX "Word: "
# define ACRONYM_PREFIX "Backronym: "

  string prompt =
    "Bacronyms are definitions as acronyms. Each word of the definition "
    "starts with "
    "the corresponding letter of the word being defined. Every word "
    "counts, even short words like \"in\" or \"the.\" Only the first "
    "letter of each word in the definition is considered. Each word "
    "must be a real word without spelling errors. The word "
    "being defined should not appear in its definition, nor conjugates.\n\n"

    "Examples:\n"
    WORD_PREFIX "fomo\n"
    ACRONYM_PREFIX "Fear Of Missing Out\n"
    WORD_PREFIX "distribute\n"
    ACRONYM_PREFIX "Deliver Items Systematically To Receiving Individuals By Urgent Truckloads Efficiently\n"
    WORD_PREFIX "path\n"
    ACRONYM_PREFIX "Passage Across The Hill\n"
    WORD_PREFIX "moving\n"
    ACRONYM_PREFIX "Making Oneself Veer Into Neighboring Geography\n"
    WORD_PREFIX "gap\n"
    ACRONYM_PREFIX "Gone Access Path\n"
    WORD_PREFIX "surfeit\n"
    ACRONYM_PREFIX "Surplus Undermining Responsible Food Eating In Teatimes\n"
    WORD_PREFIX "yolo\n"
    ACRONYM_PREFIX "You Only Live Once\n";

  llama_context * ctx = llm.lctx;

  // Facts about the vocabulary!
  // Only one token has a newline in it, which is the newline token.
  // Space only appears leading tokens, although there are a number
  // of tokens that consist of only spaces.

  std::vector<bool> starts_space;
  std::vector<bool> all_space;
  // std::vector<bool> is_ascii;
  // Only allowing A-Z, a-z, space.
  std::vector<bool> is_alphabetical;
  // ignores leading space (see above).
  // Only for tokens that consist of letters (with perhaps leading spaces).
  // The first letter will be lowercase (in a-z), or else 0.
  std::vector<char> first_letter;

  {
    const int nv = llama_n_vocab(ctx);
    printf("Vocab size: " ABLUE("%d") "\n", nv);
    for (int id = 0; id < nv; id++) {
      const string s = llama_token_to_str(ctx, id);
      starts_space.push_back(s[0] == ' ');
      all_space.push_back(AllSpace(s));
      bool alpha = IsAlphabetical(s);
      is_alphabetical.push_back(alpha);
      char c = 0;
      if (alpha) {
        int x = 0;
        // OK for this to read the 0 at the end.
        while (s[x] == ' ') x++;
        char cc = s[x] | 32;
        if (cc >= 'a' && cc <= 'z') c = cc;
      }
      first_letter.push_back(c);
    }
  }

  // Print vocabulary stats and exit.
  if (false) {
    int ascii = 0, has_newline = 0, has_space = 0, has_space_inside = 0;
    const int nv = llama_n_vocab(ctx);
    printf("Vocab size: %d\n", nv);
    for (int id = 0; id < nv; id++) {
      const char *sp = llama_token_to_str(ctx, id);
      if (sp == nullptr) {
        printf("Token %d null??\n", id);
        return -1;
      }

      string s = sp;
      if (IsAscii(s)) {
        ascii++;
      }
      if (ContainsChar(s, '\n')) {
        has_newline++;
      }

      if (ContainsChar(s, ' ')) {
        has_space++;

        if (ContainsChar(s.c_str() + 1, ' ')) {
          has_space_inside++;
          // These turn out to be strings that are all spaces.
          printf("%d [%s]\n", id, s.c_str());
        }
      }

      if (false) {
        if (sp == nullptr) {
          printf("%d nullptr\n", id);
        } else {
          std::string s = sp;
          if (IsAscii(s)) {
            printf("%d [%s]\n", id, s.c_str());
          } else {
            printf("%d [?]\n", id);
          }
        }
      }
    }

    printf("Ascii: %d\n"
           "Has newline: %d\n"
           "Has space: %d\n"
           "Non-leading space: %d\n",
           ascii,
           has_newline,
           has_space, has_space_inside);
    return 0;
  }

  {
    Timer prompt_timer;
    llm.DoPrompt(prompt);
    EmitTimer("Evaluated prompt", prompt_timer);
  }

  Timer save_state_timer;
  const LLM::State start_state = llm.SaveState();
  EmitTimer("Saved start state", save_state_timer);
  printf("Start state is ~" ABLUE("%lld") " megabytes\n",
         (int64_t)(start_state.llama_state.size() / (1024LL * 1024LL)));

  // Now expand acronyms.

  bool first = true;
  for (const string &word : words) {
    printf(AWHITE(" == ") APURPLE("%s") AWHITE (" == ") "\n",
           word.c_str());
    if (!first) {
      Timer load_state_timer;
      llm.LoadState(start_state);
      EmitTimer("Reloaded start state", load_state_timer);
    }
    first = false;

    {
      Timer word_prompt_timer;
      string word_prompt = WORD_PREFIX + word + "\n" ACRONYM_PREFIX;
      llm.InsertString(word_prompt);
      EmitTimer("Evaluated word prompt", word_prompt_timer);
    }

    string result = "";

    // The word we're currently working on.
    int word_idx = 0;
    // Have we emitted any token for this word? If so,
    // we have output its first letter.
    bool in_word = false;
    for (;;) {
      // this is size n_vocab (just the last token) if
      // params.logits_all is false (XXX assert!)
      // auto logits = llama_get_logits(ctx);

      std::unique_ptr<LLM::Candidates> candidates = llm.GetCandidates();


      int rejected = 0;
      static constexpr float IMPOSSIBLE = -1.0e28f;
      static constexpr bool FILTER_WORDS = true;
      if (FILTER_WORDS) {
        // down-weight illegal tokens.
        const bool final_word = word_idx == (int)word.size() - 1;
        for (llama_token_data &tok : *candidates) {

          // Never allow end of stream (although we could just
          // treat this like newline?)
          if (tok.id == llama_token_eos()) {
            tok.logit = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // Allow newline only if we are inside the last word.
          if (tok.id == llama_token_nl()) {
            if (in_word && final_word)
              continue;

            tok.logit = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // Can we just use -inf?
          if (!is_alphabetical[tok.id]) {
            tok.logit = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // If we're not inside the word yet, then this
          // token needs to continue the current word (or
          // start the next one).
          if (in_word) {
            // OK for the word to be all spaces; this would
            // just start a new word. (Unless this is the
            // final word).
            if (all_space[tok.id]) {
              if (final_word) {
                tok.logit = IMPOSSIBLE;
                rejected++;
              }
              continue;
            }

            // Similarly, OK for the token to start a new word,
            // as long as it has the right character.
            if (starts_space[tok.id]) {
              if (final_word || first_letter[tok.id] != word[word_idx + 1]) {
                rejected++;
                tok.logit = IMPOSSIBLE;
                continue;
              }
            }

            // Otherwise, any token is okay.

          } else {
            // Next token must start a word. So don't
            // allow spaces.
            if (all_space[tok.id]) {
              rejected++;
              tok.logit = IMPOSSIBLE;
              continue;
            }

            // Similarly, we don't allow multiple spaces between words.
            if (starts_space[tok.id]) {
              rejected++;
              tok.logit = IMPOSSIBLE;
              continue;
            }

            // And the token has to start with the right letter.
            if (first_letter[tok.id] != word[word_idx]) {
              rejected++;
              tok.logit = IMPOSSIBLE;
              continue;
            }
          }
        }
      }

      static constexpr bool SHOW_TOKENS = true;
      if (SHOW_TOKENS) {
        // XXX just use some native sorting of Candidates?
        std::vector<std::pair<string, float>> toks;
        static constexpr bool PRINT_ALL = false;
        printf("Rejected %d. Remaining: ", rejected);
        for (const llama_token_data &tok : *candidates) {
          if (tok.logit > IMPOSSIBLE) {
            if (tok.id == llama_token_nl()) {
              if (PRINT_ALL) printf("[\\n] ");
              toks.emplace_back("\\n", tok.logit);
            } else {
              string s = llama_token_to_str(ctx, tok.id);
              if (IsAscii(s)) {
                if (PRINT_ALL) printf("[%s] ", s.c_str());
                toks.emplace_back(s, tok.logit);
              } else {
                if (PRINT_ALL) printf("[??] ");
                toks.emplace_back("??", tok.logit);
              }
            }
          }
        }
        if (!PRINT_ALL) {
          printf("\n");
          std::sort(toks.begin(), toks.end(),
                    [](const std::pair<string, float> &a,
                       const std::pair<string, float> &b) {
                      return a.second > b.second;
                    });
          for (int i = 0; i < 12 && i < (int)toks.size(); i++) {
            printf("  [%s] %.9f\n", toks[i].first.c_str(), toks[i].second);
          }
        } else {
          printf("\n");
        }
      }

      // Sample it.
      int id = llm.SampleToken(LLM::SampleType::MIROSTAT_2,
                               std::move(candidates));
      // Commit the token.
      llm.TakeToken(id);

      // Advance the state.
      if (id == llama_token_nl()) {
        printf("Done. " APURPLE("%s") " = [" AYELLOW("%s") "]\n",
               word.c_str(), result.c_str());

        FILE *resultsfile = fopen("acronyms.txt", "ab");
        fprintf(resultsfile, "%s: %s\n", word.c_str(), result.c_str());
        fclose(resultsfile);
        break;
      }

      result += llama_token_to_str(ctx, id);

      if (result.size() > 120) {
        // XXX Should find a better way to get out of these ruts.
        printf(ARED("Failed.") "\n");
        break;
      }

      if (all_space[id]) {
        in_word = false;
        word_idx++;
      } else if (starts_space[id]) {
        in_word = true;
        word_idx++;
      } else {
        // just continuing a word.
        in_word = true;
      }

      printf("%s = [%s] (tok %d in %c idx %d rej %d)\n",
             word.c_str(), result.c_str(), id, in_word ? 'Y' : 'N',
             word_idx, rejected);
    }

  }



  llama_print_timings(ctx);
  llama_free(ctx);

  return 0;
}
