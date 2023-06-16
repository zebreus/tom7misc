
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
#include <optional>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"

#include "threadutil.h"
#include "util.h"
#include "llm.h"
#include "nfa.h"

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

  ContextParams cparams;
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  SamplerParams sparams;
  sparams.type = SampleType::MIROSTAT_2;

  LLM llm(cparams, sparams);
  EmitTimer("Loaded model", model_timer);

  std::vector<std::string> words = {
    "nonsensical",
    "violence",
    "mammals",
    "lives",
    "unlikely",
    "scrutiny",
    "occurrences",
    "crazily",
    "offering",
    "vivacious",
    "unifying",
    "related",
    "independent",
    "applying",
    "exciting",
    "meritless",
    "bottom",
    "grandiloquence",
    "hauling",
    "athletic",
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

  // Precomputed word sets for each letter.
  // These are ENFA type but don't actually use epsilons!
  std::vector<NFA<257>> letter_enfa;
  letter_enfa.resize(26);
  {
    std::vector<string> dict =
      Util::ReadFileToLines("../acronymine/word-list.txt");
    printf("%zu words in dictionary\n", dict.size());
    Timer letter_timer;
    std::mutex m;
    ParallelComp(26,
                 [&](int idx) {
                   const char c = 'a' + idx;
                   std::set<string> letter_words;
                   for (const string &ss : dict) {
                     string s = Util::lcase(ss);
                     if (s.size() > 1 && s[0] == c) {
                       if (!IsAlphabetical(s)) {
                         printf("Not alphabetical: %s\n", s.c_str());
                         continue;
                       }

                       // Capitalize the first letter.
                       s[0] &= ~32;
                       letter_words.insert(s);
                     }
                   }

                   letter_enfa[idx] = RegEx<256>::LiteralSet(letter_words);
                   {
                     MutexLock ml(&m);
                     auto [t, s] = letter_enfa[idx].DebugSize();
                     printf(AGREEN("%c") " " ABLUE("ENFA") " %d t %d s\n",
                            c, t, s);
                   }
                 },
                 4);
    EmitTimer("Made letter NFAs", letter_timer);
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
         (int64_t)(start_state.context_state.llama_state.size() /
                   (1024LL * 1024LL)));

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

    // Make NFA for its expansion. This only allows valid acronyms using
    // (lexical) acronymy rules.
    {
      Timer word_nfa_timer;
      using RE = RegEx<256>;
      NFA<257> word_enfa = RE::Empty();
      for (int i = 0; i < (int)word.size(); i++) {
        char c = word[i];
        CHECK(c >= 'a' && c <= 'z');
        const NFA<257> &w = letter_enfa[c - 'a'];
        if (i != 0) word_enfa = RE::Concat(word_enfa, RE::LiteralString(" "));
        word_enfa = RE::Concat(word_enfa, w);
      }
      word_enfa = RE::Concat(word_enfa, RE::LiteralString("\n"));
      auto nfa = RemoveEpsilon<256>(word_enfa);
      auto [t, s] = nfa.DebugSize();
      printf("Resulting NFA size: %d t %d s\n", t, s);
      llm.sampler.SetNFA(std::move(nfa));
      EmitTimer("set word nfa", word_nfa_timer);
    }

    auto Generate = [&]() -> std::optional<string> {
      Timer gen_timer;
      string result;
      for (;;) {
        // Get and commit a token (verbosely).
        std::unique_ptr<Context::Candidates> candidates =
          llm.context.GetCandidates();
        llm.sampler.Penalize(candidates.get());
        llm.sampler.FilterByNFA(candidates.get());
        static constexpr bool SHOW_TOKENS = true;
        if (SHOW_TOKENS) {
          llm.AnsiPrintCandidates(*candidates, 12);
        }
        int id = llm.sampler.SampleToken(std::move(candidates));

        llm.TakeTokenBatch({id});

        if (id == llama_token_nl()) {
          printf("\n");
          EmitTimer("Generated", gen_timer);
          return {result};
        }

        string tok = llm.context.TokenString(id);
        printf(AWHITE("%s") " = %s" ABLUE("%s") "\n",
               word.c_str(), result.c_str(), tok.c_str());
        result += tok;

        if (result.size() > 120) {
          // XXX Should find a better way to get out of these ruts.
          printf(ARED("Failed.") "\n");
          EmitTimer("Failed to generate", gen_timer);
          return nullopt;
        }
      }
    };

    auto so = Generate();

    if (so.has_value()) {
      string acronym = so.value();
      printf("Done. " APURPLE("%s") " = [" AYELLOW("%s") "]\n",
             word.c_str(), acronym.c_str());

      {
        FILE *resultsfile = fopen("acronyms.txt", "ab");
        fprintf(resultsfile, "%s: %s\n", word.c_str(), acronym.c_str());
        fclose(resultsfile);
      }
    }
  }

  return 0;
}
