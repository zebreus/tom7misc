
#include <cstdio>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "llm.h"
#include "models.h"
#include "timer.h"
#include "util.h"

using namespace std;

static void Vary(LLM *llm, const string &prompt, const string &original) {
  Timer startup_timer;
  llm->Reset();

  // Prompt is instructions + original text + header:
  std::string full_prompt =
    StringPrintf("%s\n"
                 "\n"
                 "Original text:\n\n"
                 "<P>%s</P>\n"
                 "\n"
                 "Rephrased text:\n\n"
                 "<P>",
                 prompt.c_str(), original.c_str());

  printf(AGREY("Full prompt: [%s]") "\n", full_prompt.c_str());

  llm->DoPrompt(full_prompt);
  // Reset regex, since the prompt may not have followed it.
  llm->sampler.SetRegEx(".*</P>");

  printf("(finished the prompt)\n");

  Timer inference_timer;
  int tokens = 0;
  for (;;) {
    // Get and commit a token.
    int id = llm->Sample();
    llm->TakeTokenBatch({id});
    string tok = llm->context.TokenString(id);
    if (id == llm->context.EOSToken())
      break;
    printf("%s", tok.c_str());
    if (llm->sampler.Accepting() || llm->sampler.Stuck())
      break;

    tokens++;
    if (tokens % 3 == 0) fflush(stdout);
  }

  printf(AGREEN("(EOS)") "\n");
}

int main(int argc, char ** argv) {
  ANSI::Init();
  CHECK(argc >= 2) << "Usage: ./variations.exe input.txt\n";

  constexpr const char *prompt_file = "variations.txt";
  const string prompt = Util::ReadFile(prompt_file);
  CHECK(!prompt.empty()) << prompt_file;

  const std::string input = Util::ReadFile(argv[1]);
  CHECK(!input.empty()) << argv[1];

  // No newlines in original.
  std::string original =
    Util::NormalizeWhitespace(Util::Replace(input, "\n", " "));

  ContextParams cparams = Models::LLAMA_7B_Q8;

  SamplerParams sparams;
  sparams.type = SampleType::MIN_P;
  sparams.min_p = 0.10;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Vary(&llm, prompt, original);

  return 0;
}
