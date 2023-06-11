
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

using namespace std;

int main(int argc, char ** argv) {
  ContextParams cparams;
  // Any model will work; use the smallest one.
  cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  SamplerParams sparams;

  LLM llm(cparams, sparams);

  auto IsAscii = [](const std::string &s) {
      for (char c : s) {
        if (c < ' ' || c > '~') return false;
      }
      return true;
    };

  const int vocab_size = llm.VocabSize();
  for (int i = 0; i < vocab_size; i++) {
    string tok = llm.TokenString(i);
    string s;
    if (i == llama_token_nl()) {
      s = "\\n";
    } else if (IsAscii(tok)) {
      s = tok;
    } else {
      s = Util::HexString(tok);
    }

    printf("%d\t[%s]\n", i, s.c_str());
  }

  return 0;
}
