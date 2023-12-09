
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

#include "nfa.h"
#include "nfa-util.h"

#include "llm.h"

#include "llm-util.h"

using namespace std;

static ByteNFA MakeRegex(const std::string &regex) {
  auto enfa = Parse(regex);
  auto nfa = RemoveEpsilon<256>(enfa);
  {
    auto [et, es] = NFADebugSize(enfa);
    auto [t, s] = NFADebugSize(nfa);
    printf("For regex: " ABLUE("%s") "\n"
           "  ENFA: %d t %d s\n"
           "  NFA: %d t %d s\n",
           regex.c_str(), et, es, t, s);
  }
  return nfa;
}

struct ChatLine {
  // The full text with no newline, like "<Tom> This is chat line."
  std::string text;
  // The state right before the line (i.e. right after the preceding
  // newline).
  LLM::State state;
  // backup of this as well
  ByteNFAMatcher user_matcher;
};

static void Chat(LLM *llm,
                 const string &user,
                 const string &prompt,
                 FILE *outfile) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

  // This basically assumes that the tokens to generate this end
  // with either ">" or "> ". I think that is the case for llama,
  // though we could check or implement this a different way.
  string user_regex =
    StringPrintf(".*\n( *<%s> ?| *\\* %s ?)", user.c_str(), user.c_str());

  auto user_nfa = MakeRegex(user_regex);
  ByteNFAMatcher user_matcher(user_nfa);

  AdvanceString("\n", &user_matcher);

  // So that we can save state whenever a chat line comes in.
  string ends_newline_regex = ".*\n";
  auto ends_newline_nfa = MakeRegex(ends_newline_regex);


  std::vector<ChatLine> lines;

  Timer startup_timer;
  llm->Reset();
  if (outfile == nullptr) {
    printf("%s", prompt.c_str());
    fflush(stdout);
  } else {
    fprintf(outfile, "%s", prompt.c_str());
  }

  // printf("NFA\n%s\n", llm->sampler.nfa.DebugString().c_str());

  llm->DoPrompt(prompt);
  // Reset regex, since the prompt may not have followed it.
  llm->sampler.ResetRegEx();

  LLM::State start_line_state = llm->SaveState();
  ByteNFAMatcher ends_newline_matcher(ends_newline_nfa);

  printf(AYELLOW("(finished the prompt)") "\n");

  // llm->sampler.SetRegEx("You can only output this.\n");

  // printf("NFA\n%s\n", llm->sampler.nfa.DebugString().c_str());

  auto NewLine = [&llm, &lines, &user_matcher]() {
      lines.emplace_back(ChatLine{
          .text = "",
          .state = llm->SaveState(),
          .user_matcher = user_matcher,
        });
    };

  // Could initialize lines from the prompt, if it contains chat
  // lines. But we have to at least have a last entry in there,
  // since that is modified in place.
  NewLine();

  Timer inference_timer;

  for (;;) {
    CHECK(!lines.empty());

    // Get and commit a token.
    int id = llm->Sample();
    llm->TakeTokenBatch({id});
    string tok = llm->context.TokenString(id);
    lines.back().text += tok;

    printf("%s", tok.c_str());
    fflush(stdout);

    // TODO: Maybe don't want to output this eagerly
    // if we have rewinding/edits/etc.
    if (outfile != nullptr) {
      fprintf(outfile, "%s", tok.c_str());
    }

    AdvanceString(tok, &ends_newline_matcher);
    AdvanceString(tok, &user_matcher);
    if (ends_newline_matcher.IsMatching()) {
      NewLine();
    }

    if (user_matcher.IsMatching()) {
      if (outfile != nullptr) {
        fflush(outfile);
      }

      printf(AWHITE(":") AGREEN("> "));
      fflush(stdout);

      string input;
      getline(cin, input);

      if (Util::TryStripPrefix("/raw ", &input)) {

        /*
        printf("(Got RAW [%s]\n", input.c_str());
        printf("NFA now:\n%s\n",
               NFADebugString(llm->sampler.nfa,
                              llm->sampler.matcher.GetStates()).c_str());
        */

        // This means we want to remove the predicted prefix and
        // then insert the input verbatim.
        input += "\n";

        lines.back().text = input;
        llm->LoadState(lines.back().state);
        // FIXME: Workaround bug :(
        llm->sampler.ResetRegEx();

        /*
        printf("NFA after load:\n%s\n",
               NFADebugString(llm->sampler.nfa,
                              llm->sampler.matcher.GetStates()).c_str());
        */

        user_matcher = lines.back().user_matcher;

        // printf("Now insert [%s]\n", input.c_str());
        /*
        for (const char c : input) {
          string cinput = StringPrintf("%c", c);
          llm->InsertString(cinput);
          AdvanceString(cinput, &user_matcher);

          printf("NFA after insert [%c]:\n%s\n", c,
                 NFADebugString(llm->sampler.nfa,
                                llm->sampler.matcher.GetStates()).c_str());

        }
        */
        llm->InsertString(input);
        AdvanceString(input, &user_matcher);

        /*
        printf("NFA after insert [%s]:\n%s\n", input.c_str(),
               NFADebugString(llm->sampler.nfa,
                              llm->sampler.matcher.GetStates()).c_str());
        */

        if (outfile != nullptr) {
          fprintf(outfile, "(raw) %s", input.c_str());
        }

        NewLine();

        /*
        printf("NFA after insert all:\n%s\n",
               NFADebugString(llm->sampler.nfa,
                              llm->sampler.matcher.GetStates()).c_str());
        */
      } else {
        // Normal chat.

        // If last token didn't include the space, insert it.
        if (tok[tok.size() - 1] != ' ') {
          input = " " + input;
        }

        input += "\n";
        llm->InsertString(input);
        AdvanceString(input, &user_matcher);

        if (outfile != nullptr) {
          fprintf(outfile, "%s", input.c_str());
        }

        NewLine();
      }
    }

    if (llm->sampler.Stuck()) {
      printf("\n(STUCK.)\n");
      return;
    }
  }
}

int main(int argc, char ** argv) {
  CHECK(argc >= 3) << "Usage: ./chat.exe User prompt.txt [out.txt]\n"
    "First line of the prompt file is a regex. For these, the regex "
    "should match \"<User> \" and \"* User \", since these are used "
    "as the antiprompt. A typical one for two participants would be\n"
    "(<(Alice|Bob)> [^\\n]+\\n|\\* (Alice|Bob) [^\\n]+\\n)*";

  string user = argv[1];
  string prompt = Util::ReadFile(argv[2]);
  printf("Prompt: [%s]\n", prompt.c_str());
  CHECK(!prompt.empty()) << argv[2];
  string regex = Util::Replace(Util::getline(prompt), "\\n", "\n");
  CHECK(!regex.empty()) << argv[2] << ": " << regex;
  // Get an early error message.
  {
    auto enfa = Parse(regex);
    auto nfa = RemoveEpsilon<256>(enfa);
    auto [et, es] = NFADebugSize(enfa);
    auto [t, s] = NFADebugSize(nfa);
    printf("Regex: " ABLUE("%s") "\n"
           "User: " AGREEN("%s") "\n"
           "Prompt size %zu.\n"
           "ENFA: %d t %d s\n"
           "NFA: %d t %d s\n",
           regex.c_str(),
           user.c_str(),
           prompt.size(), et, es, t, s);

    string chat = StringPrintf("<%s> chat\n", user.c_str());
    string action = StringPrintf("* %s action\n", user.c_str());
    CHECK(Matches(nfa, chat)) << user << ": " << chat;
    CHECK(Matches(nfa, action)) << user << ": " << action;
  }
  // AnsiInit();
  Timer model_timer;

  FILE *file = nullptr;
  if (argc >= 4) {
    file = fopen(argv[3], "wb");
    CHECK(file != nullptr);
    printf("Writing to " ACYAN("%s") "...\n", argv[3]);
  }

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";

  cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = regex;

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Chat(&llm, user, prompt, file);
  if (file != nullptr) fclose(file);

  return 0;
}
