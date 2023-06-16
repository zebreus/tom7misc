
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

static void Continue(LLM *llm, const string &prompt) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

  Timer startup_timer;
  llm->Reset();
  printf("%s", prompt.c_str());
  fflush(stdout);
  llm->DoPrompt(prompt);
  // Reset regex, since the prompt may not have followed it.
  llm->sampler.ResetRegEx();

  printf("(finished the prompt)\n");

  // llm->sampler.SetRegEx("You can only output this.\n");

  int tokens = 0;
  for (;;) {
    // Get and commit a token.
    int id = llm->Sample();
    llm->TakeTokenBatch({id});
    string tok = llm->context.TokenString(id);
    printf("%s", tok.c_str());
    tokens++;
    if (tokens % 3 == 0) fflush(stdout);
    if (llm->sampler.Stuck()) {
      printf("\n(STUCK.)\n");
      return;
    }
  }
}

int main(int argc, char ** argv) {
  CHECK(argc == 2) << "Usage: ./continue.exe prompt.txt\n"
    "First line of the prompt file is a regex; use .* for anything.";

  string prompt = Util::ReadFile(argv[1]);
  CHECK(!prompt.empty()) << argv[1];
  string regex = Util::Replace(Util::getline(prompt), "\\n", "\n");
  CHECK(!regex.empty()) << argv[1] << ": " << regex;
  // Get an early error message.
  {
    auto enfa = Parse(regex);
    auto nfa = RemoveEpsilon<256>(enfa);
    printf("Prompt size %zu.\nENFA: ", prompt.size());
    enfa.PrintDebugStats();
    printf("\nNFA: ");
    nfa.PrintDebugStats();
    printf("\n");
  }
  // AnsiInit();
  Timer model_timer;


  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-f16.bin";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = regex;

  LLM llm(cparams, sparams);
  printf("Loaded model.\n");

  Continue(&llm, prompt);

  return 0;
}
