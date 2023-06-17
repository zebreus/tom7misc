
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
#include "llm-util.h"

using namespace std;

static bool IsAlphabetical(const std::string &s) {
  for (char c : s) {
    if (c == ' ') continue;
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    return false;
  }
  return true;
}

// XXX these trailing spaces might actually be bad because
// of the way tokenization works

static string WordPrefix(const string &word) {
  return "Word: ";
}

static string AcronymPrefix(const string &word) {
  return StringPrintf("Backronym of \"%s\": ", word.c_str());
}

static string DefinitionPrefix(const string &word) {
  return StringPrintf("Normal definition of \"%s\": ", word.c_str());
}

static string BrainstormPrefix(const string &word, char c) {
  return StringPrintf("Word ideas for \"%s\" starting with %c: ",
                      word.c_str(), c & ~32);
}

int main(int argc, char ** argv) {
  using RE = RegEx<256>;

  AnsiInit();
  Timer model_timer;

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  SamplerParams sparams;
  sparams.type = SampleType::MIROSTAT_2;

  LLM llm(cparams, sparams);
  EmitTimer("Loaded model", model_timer);

  auto InsertString = [&llm](const string &s) {
      printf(AGREY("%s"), s.c_str());
      fflush(stdout);
      llm.InsertString(s);
    };

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

  struct Example {
    string word;
    string defn;
    std::map<char, std::vector<string>> brainstorm;
    string acronym;
  };

  std::vector<Example> examples = {
    Example{.word = "path",
            .defn = "A way or track laid down for walking or made by "
            "continual treading.",
            .brainstorm = {
        {'A', {"approach", "avenue", "across", "alley"}},
        {'H', {"highroad", "horizon", "heading"}},
        {'P', {"passage", "pursuit", "paved"}},
        {'T', {"trajectory", "trail", "toward", "to"}}},
            .acronym = "Passage Across The Hill",
    },
    Example{.word = "moving",
            .defn = "Gerund of moving, which means to go in a specific direction.",
            .brainstorm = {
        {'M', {"making", "marching", "migrate"}},
        {'O', {"oneself", "orient"}},
        {'V', {"veer", "venture", "venturing"}},
        {'I', {"inching"}},
        {'N', {"nudge", "near", "navigate"}},
        {'G', {"gravitate", "go", "getting"}},
            },
            .acronym = "Making Oneself Veer Into Neighboring Geography",
    },
  };

    /*
    WORD_PREFIX "fomo\n"
    ACRONYM_PREFIX "Fear Of Missing Out\n"
    WORD_PREFIX "distribute\n"
    ACRONYM_PREFIX "Deliver Items Systematically To Receiving Individuals By Urgent Truckloads Efficiently\n"
    WORD_PREFIX "gap\n"
    ACRONYM_PREFIX "Gone Access Path\n"
    WORD_PREFIX "surfeit\n"
    ACRONYM_PREFIX "Surplus Undermining Responsible Food Eating In Teatimes\n"
    WORD_PREFIX "yolo\n"
    ACRONYM_PREFIX "You Only Live Once\n";
    */


  string prompt =
    "Bacronyms are definitions as acronyms. Each word of the definition "
    "starts with "
    "the corresponding letter of the word being defined. Every word "
    "counts, even short words like \"in\" or \"the.\" Only the first "
    "letter of each word in the definition is considered. Each word "
    "must be a real word without spelling errors. The word "
    "being defined should not appear in its definition, nor conjugates.\n\n"

    "It can be useful to brainstorm words that start with the letters first."
    "\nExamples:\n";

  for (const Example &example : examples) {
    StringAppendF(&prompt, "%s%s",
                  WordPrefix(example.word).c_str(),
                  example.word.c_str());
    StringAppendF(&prompt, "%s%s",
                  DefinitionPrefix(example.word).c_str(),
                  example.defn.c_str());
    for (const auto &[c, v] : example.brainstorm) {
      StringAppendF(&prompt, "%s",
                    BrainstormPrefix(example.word, c).c_str());
      for (const string &s : v)
        StringAppendF(&prompt, "%s, ", s.c_str());
      StringAppendF(&prompt, "\n");
    }

    StringAppendF(&prompt, "%s%s",
                  AcronymPrefix(example.word).c_str(),
                  example.acronym.c_str());
  }

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
    printf(AGREY("%s") "\n", prompt.c_str());
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
      string word_prompt = StringPrintf("%s%s\n",
                                        WordPrefix(word).c_str(),
                                        word.c_str());
      InsertString(word_prompt);
      EmitTimer("Evaluated word prompt", word_prompt_timer);
    }


    // Make these optional. All these do is (hopefully?) condition the
    // transformer to generate better acronyms below.

    // Definition.
    {
      Timer define_timer;
      InsertString(DefinitionPrefix(word));
      llm.sampler.SetRegEx("[^\\n]*\n");
      auto [defn, _] = llm.GenerateUntilEx("\n", 120, true);
      printf("\n" AYELLOW("Definition") ": %s\n", defn.c_str());
      EmitTimer("defined", define_timer);
    }

    // Brainstorm some words.
    // (Alternatively, we could just insert these from word2vec).
    {
      Timer brainstorm_timer;
      std::set<char> letters;
      for (char c : word) letters.insert(c);
      for (char c : letters) {
        CHECK(c >= 'a' && c <= 'z');
        InsertString(BrainstormPrefix(word, c));
        // static constexpr int NUM_STORM = 3;
        // XXX wrong because we don't actually want to end with a comma.
        NFA<257> enfa =
          RE::Concat(RE::Plus(RE::Concat(letter_enfa[c - 'a'],
                                         RE::LiteralString(", "))),
                     RE::LiteralString("\n"));
        auto nfa = RemoveEpsilon<256>(enfa);
        llm.sampler.SetNFA(std::move(nfa));
        auto [storm, _] = llm.GenerateUntilEx("\n", 40, true);
        printf("\n" ACYAN("%c ideas") ": %s\n", c, storm.c_str());
      }
      EmitTimer("brainstormed", brainstorm_timer);
    }

    InsertString(AcronymPrefix(word));

    // Make NFA for its expansion. This only allows valid acronyms using
    // (lexical) acronymy rules.
    {
      Timer word_nfa_timer;
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

        // This should no longer actually be possible, since we limit
        // to words in the actual dictionary.
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
