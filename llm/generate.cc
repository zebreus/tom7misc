
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

using namespace std;

static std::string AnsiTime(double seconds) {
  if (seconds < 1.0) {
    return StringPrintf(AYELLOW("%.2f") "ms", seconds * 1000.0);
  } else if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "h"
                        AYELLOW("%d") "m"
                        AYELLOW("%02d") "s",
                        ohour, omin, osec);
  }
}

static void EmitTimer(const std::string &name, const Timer &timer) {
  printf(AWHITE("%s") " in %s\n",
         name.c_str(),
         AnsiTime(timer.Seconds()).c_str());
}

static void Generate(LLM *llm) {
  string prompt =
    /*
    "// Single-file text adventure game. (c) 1984 Tom Murphy VII\n"
    "// Language: C++\n"
    "// Dependencies: None\n"
    "// Lines: 4000\n"
    "\n"
    "#include <cstdio>\n"
    "\n"
    "using namespace std;\n"
    "\n"
    "int main(int argc, char **argv) {\n";
    */

    /*
    "Transcript of Alice and Bob arguing about whether or not a "
    "hotdog is a sandwich. Alice takes the view that the hotdog "
    "is a sandwich and makes a logical case for this. Bob feels "
    "that this can't be right, but he can't quite explain why "
    "it doesn't fit Alice's definition. They argue for quite some "
    "time, but remain collegiate. Alice never convinces Bob.\n"
    "<Alice> A hotdog is a sandwich.\n"
    "<Bob> That doesn't sound right. Why?\n";
    */

    /*
    (string)"The follow is text containing an argument between "
      "Alice and Bob, followed by a 100-word summary of the discussion "
      "so far.\n"
      "-----------\n"
      "Argument:\n" +
    Util::ReadFile("argument.txt") +
      "-----------\n" +
      "100 word summary:\n";
    */

    "The following is a recently-discovered proof by Gauss of the "
    "Collatz conjecture. It works by defining a metric GM for every integer. "
    "This metric always reduced by the Collatz function, except on a finite "
    "set of small numbers for which we know the Collatz function enters a "
    "cycle. The Collatz conjecture thus follows by a simple induction. "
    "\n"
    "Let's begin with a reminder of the Collatz conjecture. ";

  Timer startup_timer;
  llm->Reset();
  printf(AGREY("%s"), prompt.c_str());
  llm->DoPrompt(prompt);

  int tokens = 0;
  for (;;) {
    // Get and commit a token.
    string tok = llm->SampleAndTake();
    printf("%s", tok.c_str());
    tokens++;
    if (tokens % 500 == 0) printf(ABLUE("[%d]"), tokens);
  }
}

int main(int argc, char ** argv) {
  AnsiInit();
  Timer model_timer;

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-f16.bin";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;

  LLM llm(cparams, sparams);
  EmitTimer("Loaded model", model_timer);

  Generate(&llm);

  return 0;
}
