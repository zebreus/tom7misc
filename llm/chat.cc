
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

struct Chatting {
  ArcFour rc;
  LLM *llm = nullptr;
  const std::vector<std::string> participants;
  const string user;
  const string prompt;
  FILE *outfile = nullptr;

  Chatting(LLM *llm,
           const std::vector<std::string> &participants,
           const string &user,
           const string &prompt,
           FILE *outfile) :
    rc(StringPrintf("chat.%lld", (int64_t)time(nullptr))),
    llm(llm), participants(participants),
    user(user), outfile(outfile) {

    Initialize(prompt);

    // This basically assumes that the tokens to generate this end
    // with either ">" or "> ". I think that is the case for llama,
    // though we could check or implement this a different way.
    const string user_regex =
      StringPrintf(".*\n( *<%s> ?| *\\* %s ?)", user.c_str(), user.c_str());
    user_nfa = MakeRegex(user_regex);

    // So that we can save state whenever a chat line comes in.
    const string ends_newline_regex = ".*\n";
    ends_newline_nfa = MakeRegex(ends_newline_regex);

  }

  ByteNFA user_nfa, ends_newline_nfa;

  void Initialize(const std::string &prompt) {
    printf("Initializing prompt of %d chars\n", (int)prompt.size());

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

    printf("Finished the prompt in %s\n",
           ANSI::Time(startup_timer.Seconds()).c_str());
  }

  std::string GetOtherParticipant() {
    int idx = RandTo(&rc, participants.size() - 1);
    for (int i = 0; i < (int)participants.size(); i++) {
      if (participants[i] == user) continue;
      if (idx == 0) return participants[i];
      idx--;
    }
    CHECK(false) << "Impossible!";
    return "";
  }

  void Chat() {

    ByteNFAMatcher user_matcher(user_nfa);
    AdvanceString("\n", &user_matcher);

    std::vector<ChatLine> lines;

    ByteNFAMatcher ends_newline_matcher(ends_newline_nfa);

    auto NewLine = [this, &lines, &user_matcher]() {
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

        auto ForceLine = [this, &lines, &user_matcher, &NewLine](
            std::string full_line) {
            CHECK(full_line.back() != '\n');
            printf(AGREY("Forced [%s]") "\n", full_line.c_str());
            full_line += "\n";

            lines.back().text = full_line;
            llm->LoadState(lines.back().state);
            // FIXME: Workaround bug :(
            llm->sampler.ResetRegEx();

            user_matcher = lines.back().user_matcher;

            llm->InsertString(full_line);
            AdvanceString(full_line, &user_matcher);

            if (outfile != nullptr) {
              fprintf(outfile, "(canceled)\n%s", full_line.c_str());
            }

            NewLine();
          };

        if (Util::TryStripPrefix("/raw ", &input)) {
          // This means we want to remove the predicted prefix and
          // then insert the input verbatim.
          ForceLine(input);

        } else if (Util::TryStripPrefix("/me ", &input)) {
          // This means to remove the predicted prefix and
          // insert " * User ...".

          ForceLine(StringPrintf("* %s %s", user.c_str(), input.c_str()));

        } else if (Util::TryStripPrefix("/say ", &input)) {
          // This means to remove the predicted prefix and
          // insert " <User> ...".

          ForceLine(StringPrintf("<%s> %s", user.c_str(), input.c_str()));

        } else if (Util::TryStripPrefix("/pass ", &input)) {
          // This means to force a different user to speak.

          string other = GetOtherParticipant();
          // This could also be "act" type.
          // Another (better?) way to accomplish this would be to set a regex
          // of just others speaking...
          std::string start_line = StringPrintf("<%s>", other.c_str());

          lines.back().text = start_line;
          llm->LoadState(lines.back().state);
          // FIXME: Workaround bug :(
          llm->sampler.ResetRegEx();

          user_matcher = lines.back().user_matcher;

          llm->InsertString(start_line);
          AdvanceString(start_line, &user_matcher);

          if (outfile != nullptr) {
            fprintf(outfile, "(pass)\n%s", start_line.c_str());
          }

        } else {
          // Normal chat, continuing the prompt (whether it's <User> or * User).

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
        printf("\n(Output regex: " ARED("STUCK") ".)\n");
        printf("Last:\n");
        llm->sampler.PrintLast();
        return;
      }
    }
  }
};


int main(int argc, char ** argv) {
  ANSI::Init();

  CHECK(argc >= 3) << "Usage: ./chat.exe User prompt.txt [out.txt]\n"
    "First line of the prompt file gives a list of participants,\n"
    "separated by commas.";

  string user = argv[1];
  string prompt = Util::ReadFile(argv[2]);
  printf("Prompt: [%s]\n", prompt.c_str());
  CHECK(!prompt.empty()) << argv[2];
  std::vector<std::string> ppts = Util::Split(Util::getline(prompt), ',');
  CHECK(ContainsVector(ppts, user)) << "The user " << user << " must be "
    "in the participants list. This is case-sensitive.";

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

  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  string user_alt = Util::Join(ppts, "|");
  string chat_regex =
    StringPrintf("( *(<(%s)>|\\* (%s)) [^ \n][^\n]*\n)*",
                 user_alt.c_str(), user_alt.c_str());

  printf("Chat regex: " ABLUE("%s") "\n",
         chat_regex.c_str());

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = chat_regex;

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Chatting chatting(&llm, ppts, user, prompt, file);
  chatting.Chat();

  if (file != nullptr) fclose(file);

  return 0;
}
