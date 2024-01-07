
// This next experiment rephrases the entire text, and
// then computes a loss function over it. Rather than
// sample randomly, we sample according to the probability
// distribution.

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
#include "auto-histo.h"

#include "llm.h"
#include "llm-util.h"

using namespace std;

static constexpr int WIDTH = 70;

static void PrintGreyParity(const std::string &tok) {
  static bool odd = 0;
  if (odd) {
    printf(AFGCOLOR(125, 125, 140, "%s"), tok.c_str());
  } else {
    printf(AFGCOLOR(190, 190, 225, "%s"), tok.c_str());
  }
  odd = !odd;
}

// Characters that it's reasonable to start a line with.
#define ASCII_LINE_START "[A-Za-z0-9()\"']"
#define ASCII_CHAR "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\ ]"
#define ASCII_NOT_SPACE "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\]"

static void Rephrase(LLM *llm, const string &prompt, const string &original) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

  Timer startup_timer;
  llm->Reset();

  // Prompt is instructions + original text + header:
  std::string full_prompt =
    StringPrintf("%s\n"
                 "\n"
                 "Original text:\n\n"
                 "%s\n"
                 "\n"
                 "Rephrased text:\n\n",
                 prompt.c_str(), original.c_str());

  printf(AGREY("Full prompt: [%s]") "\n", full_prompt.c_str());

  llm->DoPrompt(full_prompt);
  printf("(finished the prompt)\n");

  // Now just complete, greedily.
  std::string text;
  for (;;) {
    std::unique_ptr<LLM::Candidates> cand = llm->context.GetCandidates();
    llm->AnsiPrintCandidates(*cand, 4);

    int id = llm->Sample();
    string tok = llm->context.TokenString(id);

    constexpr int MAX_PREFIX = 60;
    printf("%s" ABLUE("%s") "\n", text.size() > MAX_PREFIX ?
           text.substr(text.size() - MAX_PREFIX, string::npos).c_str() :
           text.c_str(),
           tok.c_str());

    text += tok;

    llm->TakeTokenBatch({id});
    if (id == llm->context.EOSToken() ||
        id == llm->context.NewlineToken()) {
      printf("Done.");
      break;
    }
  }

  printf("\n%s\n", text.c_str());
}

int main(int argc, char ** argv) {
  CHECK(argc >= 2) << "Usage: ./rephrase.exe input.txt\n";

  constexpr const char *prompt_file = "rephrase.txt";
  const string prompt = Util::ReadFile(prompt_file);
  CHECK(!prompt.empty()) << prompt_file;

  const std::string input = Util::ReadFile(argv[1]);
  CHECK(!input.empty()) << argv[1];

  // No newlines in original.
  std::string original =
    Util::NormalizeWhitespace(Util::Replace(input, "\n", " "));

  // AnsiInit();
  Timer model_timer;

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  // cparams.model = "codellama2/34b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  // Probably should use something like "minimum probability" sampling
  // here.
  sparams.type = SampleType::GREEDY;
  sparams.min_p = 0.05;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Rephrase(&llm, prompt, original);

  return 0;
}
