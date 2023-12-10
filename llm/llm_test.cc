
#include "llm.h"

#include <string>
#include <vector>
#include <cstdio>

#include "timer.h"
#include "ansi.h"

using namespace std;

static constexpr bool VERBOSE = false;

static void BasicPredict() {
  string prompt = "Please excuse my";
  Timer model_timer;
  ContextParams cparams;
  // Fastest model for test.
  // cparams.model = "llama2/7b/ggml-model-Q2_K.gguf";
  cparams.model = "llama2/7b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // Get determinism for test.
  sparams.seed = 0xCAFE;
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

    if (VERBOSE) printf("GetCandidates:\n");
    std::unique_ptr<Context::Candidates> candidates =
      llm.context.GetCandidates();
    if (VERBOSE) {
      printf(ABLUE("Original:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
    llm.sampler.Penalize(candidates.get());
    if (VERBOSE) {
      printf(ACYAN("Penalized:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
    llm.sampler.FilterByNFA(candidates.get());
    if (VERBOSE) {
      printf(APURPLE("Penalized, filtered:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
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


static void Rewind() {
  string prompt = "1 2 3 4 5 6 7 8";
  Timer model_timer;
  ContextParams cparams;
  // Fastest model for test.
  cparams.model = "e:\\llama2\\7b\\ggml-model-Q2_K.gguf";
  // cparams.model = "llama2/7b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // Get determinism for test.
  sparams.seed = 0xCAFE;
  sparams.type = SampleType::GREEDY;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");
  llm.DoPrompt(prompt);
  printf(AGREEN("Finished the prompt") ".\n");

  printf(AGREY("%s"), prompt.c_str());
  fflush(stdout);

  LLM::State state = llm.SaveState();

  // Get two tokens.

  auto SampleAndTake = [&]() {
      if (VERBOSE) printf("GetCandidates:\n");
      std::unique_ptr<Context::Candidates> candidates =
        llm.context.GetCandidates();
      if (VERBOSE) {
        printf(ABLUE("Original:") "\n");
        llm.AnsiPrintCandidates(*candidates, 10);
      }
      llm.sampler.Penalize(candidates.get());
      if (VERBOSE) {
        printf(ACYAN("Penalized:") "\n");
        llm.AnsiPrintCandidates(*candidates, 10);
      }
      llm.sampler.FilterByNFA(candidates.get());
      if (VERBOSE) {
        printf(APURPLE("Penalized, filtered:") "\n");
        llm.AnsiPrintCandidates(*candidates, 10);
      }
      const int id = llm.sampler.SampleToken(std::move(candidates));

      llm.TakeTokenBatch({id});
      return llm.context.TokenString(id);
    };

  string oldtok2;
  {
    string tok1 = SampleAndTake();
    printf(ABLUE("[%s]"), tok1.c_str());
    string tok2 = SampleAndTake();
    printf(APURPLE("(%s)"), tok2.c_str());
    string toktok = tok1 + tok2;
    // This is the only reasonable continuation.
    CHECK(toktok.find(" 9") == 0) << toktok;
    oldtok2 = tok2;
  }

  // TODO: We could save the candidates and compare them.

  printf("\n" AYELLOW("-- insert --\n") "\n");

  llm.InsertString("some tokens so that we can test rewinding.");

  printf("\n" AYELLOW("-- load --\n") "\n");
  llm.LoadState(state);
  {
    string tok1 = SampleAndTake();
    printf(ABLUE("[%s]"), tok1.c_str());
    string tok2 = SampleAndTake();
    printf(APURPLE("(%s)"), tok2.c_str());
    string toktok = tok1 + tok2;
    // This is the only reasonable continuation.
    CHECK(toktok.find(" 9") == 0) << toktok;
    CHECK(tok2 == oldtok2) << tok1 << " vs " << tok2;
  }

  printf("\n");
}

static void TokenizeInContext() {
  constexpr bool VERBOSE = false;
  string prompt = "one two three four five six seven eight nine ten "
    "eleven twelve";
  Timer model_timer;
  ContextParams cparams;
  // Fastest model for test.
  cparams.model = "e:\\llama2\\7b\\ggml-model-Q2_K.gguf";
  // cparams.model = "llama2/7b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // Get determinism for test.
  sparams.seed = 0xCAFE;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");
  llm.DoPrompt(prompt);
  printf(AGREEN("Finished the prompt") ".\n");

  printf(AGREY("%s"), prompt.c_str());
  fflush(stdout);


  LLM::State state_before_thirteen = llm.SaveState();

  // Get two tokens.
  auto SampleAndTake = [&]() {
    if (VERBOSE) printf("GetCandidates:\n");
    std::unique_ptr<Context::Candidates> candidates =
      llm.context.GetCandidates();
    if (VERBOSE) {
      printf(ABLUE("Original:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
    llm.sampler.Penalize(candidates.get());
    if (VERBOSE) {
      printf(ACYAN("Penalized:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
    llm.sampler.FilterByNFA(candidates.get());
    if (VERBOSE) {
      printf(APURPLE("Penalized, filtered:") "\n");
      llm.AnsiPrintCandidates(*candidates, 10);
    }
    const int id = llm.sampler.SampleToken(std::move(candidates));

    llm.TakeTokenBatch({id});
    return llm.context.TokenString(id);
    };

  // multiple tokenizations, including...
  // [ th] [irt] [een]
  // [ th] [ir] [teen]
  std::vector<std::string> oldtoks;
  // Represents the state before each token.
  std::vector<LLM::State> states;
  {
    std::string str;
    for (int i = 0; i < 3; i++) {
      states.push_back(llm.SaveState());
      string tok = SampleAndTake();
      printf(ABLUE("[%s]"), tok.c_str());
      oldtoks.push_back(tok);
      str += tok;
    }
    // This is the only reasonable continuation.
    CHECK(str.find(" thirteen") == 0) << str;
  }

  bool wrong = false;
  for (int pos = 0; pos < 3; pos++) {
    printf("\n" AYELLOW("-- Rewind %d --") "\n", pos);

    llm.LoadState(states[pos]);
    // Insert the previously predicted token.
    printf(APURPLE("[%s]"), oldtoks[pos].c_str());
    llm.InsertString(oldtoks[pos]);

    // Now we expect that the remainder is predicted this way.
    for (int i = pos + 1; i < 3; i++) {
      string tok = SampleAndTake();
      printf(ABLUE("[%s]"), tok.c_str());
      if (tok != oldtoks[i]) {
        printf(ARED("wanted [%s]"), oldtoks[i].c_str());
        wrong = true;
      }
    }
  }

  printf("\n");
  CHECK(!wrong);

  /*

  llm.InsertString("some tokens so that we can test rewinding.");

  printf("\n" AYELLOW("-- load --\n") "\n");

  llm.LoadState(state);
  */

  printf("\n");
}


int main(int argc, char **argv) {
  ANSI::Init();
  LLM::Init();

  BasicPredict();
  Rewind();
  TokenizeInContext();

  printf("OK\n");
  return 0;
}
