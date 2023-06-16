
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

    /*
    "The following is a recently-discovered proof by Gauss of the "
    "Collatz conjecture. It works by defining a metric GM for every integer. "
    "This metric always reduced by the Collatz function, except on a finite "
    "set of small numbers for which we know the Collatz function enters a "
    "cycle. The Collatz conjecture thus follows by a simple induction. "
    "\n"
    "Let's begin with a reminder of the Collatz conjecture. ";
    */

    /*
    "The following is an example of constrained writing. It describes the "
    "rules of tennis without ever using the letter 'a'!\n"
    "\n";
    llm->sampler.SetRegEx("[^Aa]*");
    */

    "The following is a transcript of a robot's work as it "
    "moves objects around a warehouse. A line that starts with THOUGHT: "
    "is the robot's thought in natural language. A line PLACE [A] ON [B] "
    "places the object A (which must be at the top of its pile) on top of "
    "B (which must have nothing above it). The line DONE means that the "
    "robot thinks it has completed its goal. No other lines appear.\n"
    "Stack 1: A ON B ON X ON Z\n"
    "Stack 2: Y ON C\n"
    "Stack 3: D\n"
    "Goal: C on Z\n"
    "\n";

  Timer startup_timer;
  llm->Reset();
  printf(AGREY("%s"), prompt.c_str());
  llm->DoPrompt(prompt);
  /*
  llm->sampler.SetRegEx("((THOUGHT: [^\\n]*\n)|"
                        "(PLACE \\[[A-Z]+\\] ON \\[[A-Z]+\\]\n))*"
                        "DONE\n");
  */
  llm->sampler.SetRegEx("You can only output this.\n");
  int tokens = 0;
  for (;;) {
    // Get and commit a token.
    int id = llm->Sample();
    llm->TakeTokenBatch({id});
    string tok = llm->context.TokenString(id);

    printf(AGREY("[") APURPLE("%d") AGREY("]") "%s",
           id,
           tok.c_str());
    tokens++;
    if (tokens % 500 == 0) printf(ABLUE("[%d]"), tokens);
    if (llm->sampler.Stuck()) {
      printf("\n" ARED("STUCK.") "\n");
      return;
    } else {
      printf(AGREEN("."));
    }
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
