
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
#include "util.h"
#include "vector-util.h"
#include "arcfour.h"
#include "randutil.h"

#include "llm.h"

#include "llm-util.h"

using namespace std;


static void AdvanceString(const string &s, NFAMatcher<256> *matcher) {
  for (int i = 0; i < (int)s.size(); i++) {
    unsigned char c = s[i];
    matcher->Advance(c);
  }
}

static bool Matches(const NFA<256> &nfa, const string &s) {
  nfa.CheckValidity();
  NFAMatcher<256> matcher(nfa);
  AdvanceString(s, &matcher);
  return matcher.IsMatching();
}

static void Chat(LLM *llm,
                 const string &user,
                 const string &prompt,
                 FILE *outfile) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

  // This basically assumes that the tokens to generate this end
  // with either ">" or "> ". I think that is the case for llama,
  // though we could check or implement this a different way.
  string user_regex =
    StringPrintf(".*\n(<%s> ?|\\* %s ?)", user.c_str(), user.c_str());

  auto user_enfa = Parse(user_regex);
  auto user_nfa = RemoveEpsilon<256>(user_enfa);
  {
    auto [et, es] = user_enfa.DebugSize();
    auto [t, s] = user_nfa.DebugSize();
    printf("User ENFA: %d t %d s\n"
           "User NFA: %d t %d s\n", et, es, t, s);
  }
  NFAMatcher<256> user_matcher(user_nfa);

  Timer startup_timer;
  llm->Reset();
  if (outfile == nullptr) {
    printf("%s", prompt.c_str());
    fflush(stdout);
  } else {
    fprintf(outfile, "%s", prompt.c_str());
  }

  // printf("NFA\n%s\n", llm->sampler.nfa.DebugString().c_str());

  llm->DoPrompt(prompt);
  // Reset regex, since the prompt may not have followed it.
  llm->sampler.ResetRegEx();

  printf(AYELLOW("(finished the prompt)") "\n");

  // llm->sampler.SetRegEx("You can only output this.\n");

  // printf("NFA\n%s\n", llm->sampler.nfa.DebugString().c_str());

  Timer inference_timer;
  const int tokens_left =
    llm->context.ContextSize() - llm->context.NumLast();
  int tokens = 0;
  for (;;) {
    // Get and commit a token.
    int id = llm->Sample();
    llm->TakeTokenBatch({id});
    string tok = llm->context.TokenString(id);

    printf("%s", tok.c_str());
    fflush(stdout);

    if (outfile != nullptr) {
      fprintf(outfile, "%s", tok.c_str());
    }

    AdvanceString(tok, &user_matcher);
    if (user_matcher.IsMatching()) {
      if (outfile != nullptr) {
        fflush(outfile);
      }

      printf(AWHITE(":") AGREEN("> "));
      fflush(stdout);

      string input;
      getline(cin, input);

      // If last token didn't include the space, insert it.
      if (tok[tok.size() - 1] != ' ') {
        input = " " + input;
      }

      input += "\n";
      printf("inserting [%s]\n", input.c_str());

      llm->InsertString(input);
      AdvanceString(input, &user_matcher);

      if (outfile != nullptr) {
        fprintf(outfile, "%s", input.c_str());
      }
    }

    if (llm->sampler.Stuck()) {
      printf("\n(STUCK.)\n");
      return;
    }
  }
}

int main(int argc, char ** argv) {
  CHECK(argc >= 3) << "Usage: ./chat.exe User prompt.txt [out.txt]\n"
    "First line of the prompt file is a regex. For these, the regex "
    "should match \"<User> \" and \"* User \", since these are used "
    "as the antiprompt. A typical one for two participants would be\n"
    "(<(Alice|Bob)> [^\\n]+\\n|\\* (Alice|Bob) [^\\n]+\\n)*";

  string user = argv[1];
  string prompt = Util::ReadFile(argv[2]);
  printf("Prompt: [%s]\n", prompt.c_str());
  CHECK(!prompt.empty()) << argv[2];
  string regex = Util::Replace(Util::getline(prompt), "\\n", "\n");
  CHECK(!regex.empty()) << argv[2] << ": " << regex;
  // Get an early error message.
  {
    auto enfa = Parse(regex);
    auto nfa = RemoveEpsilon<256>(enfa);
    auto [et, es] = enfa.DebugSize();
    auto [t, s] = nfa.DebugSize();
    printf("Regex: " ABLUE("%s") "\n"
           "User: " AGREEN("%s") "\n"
           "Prompt size %zu.\n"
           "ENFA: %d t %d s\n"
           "NFA: %d t %d s\n",
           regex.c_str(),
           user.c_str(),
           prompt.size(), et, es, t, s);

    string chat = StringPrintf("<%s> chat\n", user.c_str());
    string action = StringPrintf("* %s action\n", user.c_str());
    CHECK(Matches(nfa, chat)) << user << ": " << chat;
    CHECK(Matches(nfa, action)) << user << ": " << action;
  }
  // AnsiInit();
  Timer model_timer;

  FILE *file = nullptr;
  if (argc >= 4) {
    file = fopen(argv[3], "wb");
    CHECK(file != nullptr);
    printf("Writing to " ACYAN("%s") "...\n", argv[3]);
  }

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  cparams.model = "llama2/7b/ggml-model-q4_0.gguf";
  // cparams.model = "llama2/70b/ggml-model-q8_0.gguf";
  // cparams.model = "llama2/70b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = regex;

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Chat(&llm, user, prompt, file);
  if (file != nullptr) fclose(file);

  return 0;
}
