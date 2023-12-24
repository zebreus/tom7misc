
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

static void Continue(LLM *llm, const string &prompt, FILE *outfile) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

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

  printf("(finished the prompt)\n");

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
    if (outfile == nullptr) {
      printf("%s", tok.c_str());
    } else {
      fprintf(outfile, "%s", tok.c_str());
    }

    tokens++;
    if (outfile == nullptr) {
      if (tokens % 3 == 0) fflush(stdout);
    } else {
      if (tokens % 3 == 0) fflush(outfile);
      printf(ANSI_PREVLINE ANSI_BEGINNING_OF_LINE ANSI_CLEARLINE
             ANSI_BEGINNING_OF_LINE "%s\n",
             ANSI::ProgressBar(tokens,
                               tokens_left,
                               "inference",
                               inference_timer.Seconds()).c_str());
    }
    if (llm->sampler.Stuck()) {
      printf("\n(STUCK.)\n");
      return;
    }
  }
}

int main(int argc, char ** argv) {
  CHECK(argc >= 2) << "Usage: ./continue.exe prompt.txt [out.txt]\n"
    "First line of the prompt file is a regex; use .* for anything.";

  string prompt = Util::ReadFile(argv[1]);
  CHECK(!prompt.empty()) << argv[1];
  string regex = Util::Replace(Util::getline(prompt), "\\n", "\n");
  CHECK(!regex.empty()) << argv[1] << ": " << regex;
  // TODO: Better errors when I am missing the regex; a common mistake!

  // Get an early error message.
  {
    auto enfa = Parse(regex);
    auto nfa = RemoveEpsilon<256>(enfa);
  }
  // AnsiInit();
  Timer model_timer;

  FILE *file = nullptr;
  if (argc >= 3) {
    file = fopen(argv[2], "wb");
    CHECK(file != nullptr);
    printf("Writing to " ACYAN("%s") "...\n", argv[2]);
  }

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // cparams.model = "llama2/70b/ggml-model-q8_0.gguf";
  cparams.model = "llama2/70b/ggml-model-f16.gguf";

  // cparams.model = "codellama2/34b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = regex;

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Continue(&llm, prompt, file);
  if (file != nullptr) fclose(file);

  return 0;
}
