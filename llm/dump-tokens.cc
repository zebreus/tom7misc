
#include <cstdio>
#include <string>

#include "util.h"
#include "llm.h"
#include "models.h"

using namespace std;

int main(int argc, char ** argv) {
  // Any model will work; use the smallest one.
  ContextParams cparams = Models::LLAMA_7B_Q2;
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
    if (i == llm.context.NewlineToken()) {
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
