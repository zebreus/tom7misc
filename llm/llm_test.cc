
#include "llm.h"

#include <string>
#include <vector>
#include <cstdio>

#include "timer.h"
#include "ansi.h"

using namespace std;

static void BasicPredict() {
  string prompt = "Please excuse my";
  Timer model_timer;
  ContextParams cparams;
  // Get determinism for test.
  cparams.seed = 0xCAFE;
  // Fastest model for test.
  cparams.model = "llama2/7b/ggml-model-q4_0.gguf";

  SamplerParams sparams;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");
  llm.DoPrompt(prompt);
  printf(AGREEN("Finished the prompt") ".\n");

  printf(AGREY("%s"), prompt.c_str());
  fflush(stdout);

  int num_tokens = 0;
  while (llm.context.NumLast() < llm.context.ContextSize()) {
    // Get and commit a token.

    printf("GetCandidates:\n");
    std::unique_ptr<Context::Candidates> candidates =
      llm.context.GetCandidates();
    printf(ABLUE("Original:") "\n");
    llm.AnsiPrintCandidates(*candidates, 10);
    llm.sampler.Penalize(candidates.get());
    printf(ACYAN("Penalized:") "\n");
    llm.AnsiPrintCandidates(*candidates, 10);
    llm.sampler.FilterByNFA(candidates.get());
    printf(APURPLE("Penalized, filtered:") "\n");
    llm.AnsiPrintCandidates(*candidates, 10);
    const int id = llm.sampler.SampleToken(std::move(candidates));

    llm.TakeTokenBatch({id});
    string tok = llm.context.TokenString(id);
    num_tokens++;
    printf(ABLUE("%s"), tok.c_str());
    fflush(stdout);
    if (tok == "\n") {
      printf(AGREEN("DONE.") "\n");
      fflush(stdout);
      break;
    }
  }

  printf("\n");
  printf("End-to-end generated %d tokens in %s.\n",
         num_tokens,
         ANSI::Time(model_timer.Seconds()).c_str());
}


int main(int argc, char **argv) {
  ANSI::Init();

  BasicPredict();

  printf("OK\n");
  return 0;
}
