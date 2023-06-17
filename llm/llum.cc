
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
#include "um.h"
#include "llm-util.h"

// Prevent runaway (no correct answer can be longer).
static constexpr int MAX_ANSWER = 80;
static constexpr int MAX_THOUGHT = 160;

static constexpr bool USE_THOUGHT = true;

using namespace std;

// With a function that generates tokens for the input, buffer them
// so that they can be read one-at-a-time. However, discard any
// strings of the form sentinel_start...sentinel_end.
struct BufferedInput {
  BufferedInput(const std::string &sentinel_start,
                const std::string &sentinel_end) :
    sentinel_start(sentinel_start),
    sentinel_end(sentinel_end) {
    CHECK(sentinel_start.find(sentinel_end) == string::npos) << "Sentinel "
      "start can't contain sentinel end!";
  }

  void Insert(const string &s) { buffer += s; }

  char GetChar(const std::function<string()> &GetToken,
               const std::function<void(string)> &Bracketed) {
    for (;;) {
      if (MaybeHasSentinel(buffer)) {
        // Includes empty buffer.
        std::string tok = GetToken();
        buffer += tok;
        if (Util::StartsWith(buffer, sentinel_start)) {
          auto pos = buffer.find(sentinel_end);
          // PERF don't need to keep testing MaybeHasSentinel in this
          // case.
          if (pos == string::npos)
            continue;

          // Otherwise, remove the string bracketed by the sentinels.
          string bracketed = buffer.substr(0, pos);
          CHECK(sentinel_start.size() <= bracketed.size());
          bracketed = bracketed.substr(sentinel_start.size(), string::npos);
          Bracketed(bracketed);
          CHECK(buffer.size() <= pos + sentinel_end.size());
          buffer = buffer.substr(pos + sentinel_end.size(), string::npos);
        }
        // Grow string until we know it doesn't start with the sentinel.
        continue;
      }

      // We have characters but they don't match the sentinel.
      CHECK(!buffer.empty());
      char c = buffer[0];
      buffer.erase(buffer.begin());
      return c;
    }
  }

private:
  bool MaybeHasSentinel(const std::string &s) {
    int p = std::min((int)s.size(), (int)sentinel_start.size());
    return s.substr(0, p) == sentinel_start.substr(0, p);
  }

  std::string buffer;
  const std::string sentinel_start, sentinel_end;
};

static void RunUM(LLM *llm) {
  string prompt =
    "Walkthrough of the Cult of the Bound Variable's CODEX, a text-based "
    "programming adventure game / puzzle. The player communicates only "
    "via text with the game, entering unix-like commands and uploading "
    "programs in order to solve the challenges. The transcript includes "
    "commentary of the player's thought process, which is not input to the "
    "game. Thoughts are written on one line in brackets like this:\n"
    "[THINK It looks like I need to log in to the system first.]\n"
    "----------\n";

  Timer startup_timer;
  llm->Reset();
  printf(AGREY("%s"), prompt.c_str());
  llm->DoPrompt(prompt);

  std::vector<uint8_t> codex_bytes = Util::ReadFileBytes("codex.um");

  for (;;) {
    printf(ARED("--reset--") "\n");
    UM um(codex_bytes);

    // input (to UM) buffer.
    BufferedInput input("[THINK ", "]\n");

    // Force these lines to be 'predicted' at the beginning. We don't
    // do this as part of the prompt, because we want them to be appropriately
    // interspersed with UM output.
    std::vector<string> initial_lines = {
      "howie",
      "xyzzy",
      "[THINK looks like a unix prompt. Maybe I can list files with 'ls']",
      "ls",
      "[THINK ah, let's look at the README file with 'cat']",
      "cat README",
      "[THINK I can run the adventure game by executing it.]",
      "./adventure",
    };

    // output (from UM) buffer.
    std::string outbuffer;
    auto GetChar = [&initial_lines, &input, &outbuffer, llm]() {
        // When UM is waiting for us, read any text it has sent.
        if (!outbuffer.empty()) {
          printf(AGREY("%s"), outbuffer.c_str());
          llm->InsertString(outbuffer);
          outbuffer.clear();
        }

        auto GetToken = [&initial_lines, llm]() {
            if (!initial_lines.empty()) {
              string ret = initial_lines[0] + "\n";
              initial_lines.erase(initial_lines.begin());
              llm->InsertString(ret);
              return ret;
            }

            return llm->SampleAndTake();
          };

        auto Bracketed = [](const string &thought) {
            printf(AWHITE("[") AYELLOW("THINK ") ABLUE("%s") AWHITE("]") "\n",
                   thought.c_str());
          };

        const char c = input.GetChar(GetToken, Bracketed);

        printf("%c", c);
        return c;
      };

    auto PutChar = [&outbuffer](char c) {
        outbuffer.push_back(c);
      };

    um.Run(GetChar, PutChar);
  }
}

int main(int argc, char ** argv) {
  AnsiInit();
  Timer model_timer;

  ContextParams cparams;
  cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // lparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // lparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // lparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // lparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // lparams.model = "../llama/models/65B/ggml-model-f16.bin";
  // lparams.mirostat = 2;
  // LLM::SampleType sample_type = LLM::SampleType::GREEDY;
  SamplerParams sparams;
  sparams.type = SampleType::MIROSTAT_2;

  LLM llm(cparams, sparams);
  EmitTimer("Loaded model", model_timer);

  RunUM(&llm);

  return 0;
}
