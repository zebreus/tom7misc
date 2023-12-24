
// Simple greedy experiment.

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
    printf(AFGCOLOR(155, 155, 160, "%s"), tok.c_str());
  } else {
    printf(AFGCOLOR(180, 180, 200, "%s"), tok.c_str());
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

  // Now we greedily re-insert text.
  // For this version, we don't even try to preserve lines.
  // So we just want to keep retrying until we have a word
  // ending at exactly WIDTH.

  std::vector<std::string> lines;

  std::unique_ptr<AutoHisto> len_histo;
  auto ResetHisto = [&len_histo]() {
      len_histo.reset(new AutoHisto(1000));
    };
  ResetHisto();

  LLM::State line_beginning = llm->SaveState();
  for (;;) {
    // As a loop invariant, line_beginning is the current state.
    std::string line;
    llm->NewRNG();
    // No newlines allowed, either.

    const bool first_line = lines.empty();
    // must start with non-space. We also exclude various
    // punctuation that wouldn't make sense.
    string first_char_regex;
    if (first_line) {
      first_char_regex = ASCII_LINE_START;
    } else {
      // But! For lines after the first, we want the LLM to think
      // it's inserting a space, since we did insert a line break.
      first_char_regex = " ";
    }

    llm->sampler.SetRegEx(
        first_char_regex +
        // Then we can have anything except two spaces in a row,
        // and we can't end with space.
        "(" ASCII_NOT_SPACE "| " ASCII_NOT_SPACE ")*");

    // Sample tokens for this line.
    // XXX this needs to sample *words*. Otherwise we'll just
    // end a line in the middle of a token, which has no good
    // continuation. ("He studied viol")
    while (line.size() < WIDTH) {
      CHECK(!llm->sampler.Stuck()) << "STUCK!";
      int id = llm->Sample();
      llm->TakeTokenBatch({id});
      string tok = llm->context.TokenString(id);

      // At the beginning of a continuation line, a space was
      // covered by the newline that we implicitly have (and
      // which the LLM does not see).
      if (!first_line && line.empty()) {
        tok = Util::LoseWhiteL(tok);
      }
      line += tok;
      PrintGreyParity(tok);
    }

    printf("\n(Length: %d)\n", (int)line.size());

    len_histo->Observe(line.size());

    if (line.size() == WIDTH) {
      // Good!
      lines.push_back(line);
      printf(AGREEN("So far:") "\n"
             "%s\n", std::string(WIDTH, '-').c_str());

      for (const string &line : lines) {
        printf("%s\n", line.c_str());
      }
      line_beginning = llm->SaveState();
      ResetHisto();
      // and continue with the next line.
    } else {
      printf(ARED("Failed:") "\n"
             "%s\n%s\n",
             std::string(WIDTH, '-').c_str(),
             line.c_str());

      printf("Histo:\n"
             "%s\n",
             len_histo->SimpleANSI(12).c_str());

      // Otherwise, try again.
      llm->LoadState(line_beginning);
    }
  }

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
  cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  // cparams.model = "codellama2/34b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Rephrase(&llm, prompt, original);

  return 0;
}
