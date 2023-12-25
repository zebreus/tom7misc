
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

// TODO: Increase weights of tokens from input.

// Tokens are not typically full words. As we fill a line,
// we make sure that we add a word at a time, so that we
// don't inadvertently end a line in the middle of a word.
struct WordStream {
  explicit WordStream(LLM *llm) : llm(llm) {
  }

  // Returns the word (whitespace etc. intact).
  std::string Next() {
    CHECK(!llm->sampler.Stuck()) << "STUCK!";

    std::string partial_word;
    for (;;) {
      // We will definitely fail in this case (and probably
      // the model is off the rails), so just return it.
      if (partial_word.size() > WIDTH) return partial_word;

      // Sample, but do not yet take, the token.
      int id = llm->Sample();
      string tok = llm->context.TokenString(id);

      // We have a "word" if the new partial_word contains a space.
      //
      // Since spaces only lead tokens, this happens when the newly
      // predicted token starts with a space. And then the save state
      // is the one right before this token.
      //
      // Note that the "word" here could be something that ends a
      // sentence, like "finished." or "\"finishing,\"". Basically we
      // rely on the model's own notion of word breaks.
      //
      // TODO: We could insist that the next token is appropriate
      // for after a line break (e.g. it should not be " --").

      if (!partial_word.empty() && tok[0] == ' ') {
        // PERF: We could keep the sampled token for the next
        // call, if we want, saving a little time.
        return partial_word;
      } else {
        // Otherwise, keep building up the word.
        // We accept the token.
        llm->TakeTokenBatch({id});
        partial_word += tok;
      }
    }
  }
private:
  LLM *llm = nullptr;
};

static std::string LengthIndicator() {
  return std::string(WIDTH, '-');
}

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

  std::vector<std::string> failures;

  LLM::State line_beginning = llm->SaveState();
  for (;;) {
    printf("Start loop. Last:\n");
    printf(ANSI_GREY);
    llm->sampler.PrintLast();
    printf(ANSI_RESET);
    printf("\n");

    for (const std::string &line : lines) {
      printf(AFGCOLOR(200, 250, 200, "%s") "\n", line.c_str());
    }

    for (const std::string &line : failures) {
      printf(AFGCOLOR(190, 50, 50, "%s") "\n", line.c_str());
    }

    printf("%s\n", LengthIndicator().c_str());

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
      //
      // We then require a line-starting character.
      first_char_regex = " " ASCII_LINE_START;
    }

    llm->sampler.SetRegEx(
        first_char_regex +
        // Then we can have anything except two spaces in a row,
        // and we can't end with space.
        "(" ASCII_NOT_SPACE "| " ASCII_NOT_SPACE ")*");

    WordStream word_stream(llm);

    // Sample words for this line.
    while (line.size() < WIDTH) {
      string word = word_stream.Next();

      // At the beginning of a continuation line, a space was
      // covered by the newline that we implicitly have (and
      // which the LLM does not see).
      if (!first_line && line.empty()) {
        word = Util::LoseWhiteL(word);
      }
      line += word;
      PrintGreyParity(word);
    }

    printf("\n(Length: %d)\n", (int)line.size());

    len_histo->Observe(line.size());

    if (line.size() == WIDTH) {
      // Good!
      lines.push_back(line);
      /*
      printf(AGREEN("So far:") "\n"
             "%s\n", LengthIndicator().c_str());
      */
      /*
      for (const string &line : lines) {
        printf("%s\n", line.c_str());
      }
      */
      line_beginning = llm->SaveState();
      failures.clear();
      ResetHisto();
      // and continue with the next line.
    } else {
      printf(ARED("Failed:") "\n"
             "%s\n%s\n",
             LengthIndicator().c_str(),
             // std::string(WIDTH, '-').c_str(),
             line.c_str());

      printf("Histo:\n"
             "%s\n",
             len_histo->SimpleHorizANSI(12).c_str());

      failures.push_back(line);

      // [ Californ]
      // llm->TakeTokenBatch({7599});
      // llm->InsertString(". In Pittsburgh, Pennsylvan");

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
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  // cparams.model = "codellama2/34b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  // Probably should use something like "minimum probability" sampling
  // here.
  // sparams.type = SampleType::MIROSTAT_2;
  sparams.type = SampleType::MIN_P;
  sparams.min_p = 0.05;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Rephrase(&llm, prompt, original);

  return 0;
}
