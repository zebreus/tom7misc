
// Multi-party chat. This uses structured messages (IRC style) which
// makes it cleaner to manipulate the chat and keep on topic.
//
// After the preamble (prompt) the chat is a series of lines. Each one
// is of the form
// <Participant> Some text they say.\n
// or
//  * Participant does some action.\n"

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

#include "console.h"

using namespace std;

static constexpr bool VERBOSE = false;

static ByteNFA MakeNFA(const std::string &regex) {
  auto enfa = Parse(regex);
  auto nfa = RemoveEpsilon<256>(enfa);
  if (VERBOSE) {
    auto [et, es] = NFADebugSize(enfa);
    auto [t, s] = NFADebugSize(nfa);
    printf("For regex: " ABLUE("%s") "\n"
           "  ENFA: %d t %d s\n"
           "  NFA: %d t %d s\n",
           regex.c_str(), et, es, t, s);
  }
  return nfa;
}

// A completed line in the chat's history.
struct ChatLine {
  std::string participant;
  // The full text with no newline or leading space like "Hello!"
  // in "<Tom> Hello!" or "slaps llama2 around a bit with a large trout"
  // in " * Tom slaps llama2 around a bit with a large trout".
  std::string text;
  // The state right before the line (i.e. right after the preceding
  // newline).
  LLM::State state;
  bool is_action = false;
};

struct LineInProgress {
  // ...
};

static std::string MakeParticipantRegex(
    const std::vector<std::string> &ppts) {
  string user_alt = Util::Join(ppts, "|");
  return StringPrintf("((<(%s)>| \\* (%s)) [^ \n][^\n]*\n)*",
                      user_alt.c_str(), user_alt.c_str());
}

struct Chatting {
  ArcFour rc;
  LLM *llm = nullptr;
  const std::vector<std::string> participants;
  const string user;
  const string prompt;
  Console console;

  Chatting(LLM *llm,
           const std::vector<std::string> &participants,
           const string &user,
           const string &prompt) :
    rc(StringPrintf("chat.%lld", (int64_t)time(nullptr))),
    llm(llm), participants(participants),
    user(user) {

    Initialize(prompt);

    // This basically assumes that the tokens to generate this end
    // with either ">" or "> ". I think that is the case for llama,
    // though we could check or implement this a different way.
    const string user_regex =
      StringPrintf("(<%s> ?| \\* %s ?)",
                   user.c_str(), user.c_str());
    user_nfa = MakeNFA(user_regex);
  }

  ByteNFA user_nfa;

  // Reset the sampler regex to allow any participant to speak,
  // when we're at the beginning of a line.
  void ResetRegex() {
    llm->sampler.SetRegEx(MakeParticipantRegex(participants));
  }

  void Initialize(const std::string &prompt) {
    printf("Initializing prompt of %d chars\n", (int)prompt.size());

    Timer startup_timer;
    llm->Reset();
    printf("%s", prompt.c_str());
    fflush(stdout);

    // printf("NFA\n%s\n", llm->sampler.nfa.DebugString().c_str());

    llm->DoPrompt(prompt);
    // Reset regex, since the prompt may not have followed it.
    ResetRegex();

    // XXX add lines to history with FinishLine.

    current_line.clear();
    start_line_state = llm->SaveState();

    printf("Finished the prompt in %s\n",
           ANSI::Time(startup_timer.Seconds()).c_str());
  }

  // Get all the NPC participants.
  std::vector<std::string> GetOtherParticipants() const {
    std::vector<std::string> others;
    for (int i = 0; i < (int)participants.size(); i++) {
      if (participants[i] != user) {
        others.push_back(participants[i]);
      }
    }
    return others;
  }

  // Chat history.
  std::vector<ChatLine> lines;

  // Current line.
  // No need to parse this until we have a complete line.
  std::string current_line;
  LLM::State start_line_state;

  // Render the line (no trailing newline) in color.
  std::string ANSILine(const ChatLine &line) const {
    if (line.is_action) {
      return StringPrintf(" " AWHITE("*") " %s %s",
                          line.participant.c_str(),
                          line.text.c_str());
    } else {
      return StringPrintf(AWHITE("<") "%s" AWHITE(">") " %s",
                          line.participant.c_str(),
                          line.text.c_str());
    }
  }

  // Fills in participant, text, is_action.
  std::optional<ChatLine> ParseLine(const std::string &line_in) {
    std::string line = Util::LoseWhiteR(Util::LoseWhiteL(line_in));
    if (line.empty()) return std::nullopt;

    if (line[0] == '*' && line[1] == ' ') {
      line = line.substr(2, string::npos);
      string ppt = Util::chopto(' ', line);
      ChatLine chat;
      chat.participant = std::move(ppt);
      chat.text = Util::LoseWhiteL(line);
      chat.is_action = true;
      return {chat};

    } else if (line[0] == '<') {
      line = line.substr(1, string::npos);
      string ppt = Util::chopto('>', line);
      ChatLine chat;
      chat.participant = std::move(ppt);
      chat.text = Util::LoseWhiteL(line);
      chat.is_action = false;
      return {chat};

    } else {
      return std::nullopt;
    }
  }

  // Add a line to history and reset. Usually line is current_line,
  // but this is also used to initialize history from the prompt.
  // The line should end with a newline.
  void FinishLine(const std::string &line_in) {
    if (line_in.empty() || line_in.back() != '\n') {
      printf(ARED("Should end with newline") " [%s]\n", line_in.c_str());
    }

    // TODO: parse line, push into lines.
    std::optional<ChatLine> oline = ParseLine(line_in);
    if (oline.has_value()) {
      ChatLine chatline = std::move(oline.value());
      chatline.state = std::move(start_line_state);
      lines.emplace_back(std::move(chatline));
    } else {
      printf(ARED("Couldn't parse") " [%s]\n", line_in.c_str());
      // So we just don't add it.
    }

    // Reset state.
    current_line.clear();
    start_line_state = llm->SaveState();
    ResetRegex();
  }

  // Clear anything in the current line and force the argument.
  void ForceLine(std::string full_line) {
    CHECK(full_line.back() != '\n') << "[" << full_line << "]";
    printf(AGREY("Forced [%s]") "\n", full_line.c_str());

    full_line += "\n";

    // First we back up to the beginning of the current line.
    current_line.clear();
    llm->LoadState(start_line_state);
    // XXX: This should not be necessary, but state save/load
    // has a bug with a stale reference from matcher to parent nfa :(
    ResetRegex();

    llm->InsertString(full_line);

    FinishLine(full_line);
  }

  void DoUserInput() {
    for (;;) {
      // TODO: Support multiple commands with \n or something.
      printf(AWHITE(":") AGREEN("> "));
      fflush(stdout);
      string input = console.WaitLine();
      // printf("Returned [%s]\n", input.c_str());
      // getline(cin, input);

      if (input.empty()) {
        // This is allowed so that we can just interrupt at any time
        // by sending a full (empty) line. We just loop again to
        // get another useful line.

      } else if (Util::TryStripPrefix("/raw ", &input)) {
        // This means we want to remove the predicted prefix and
        // then insert the input verbatim.
        ForceLine(input);
        return;
      } else if (Util::TryStripPrefix("/me ", &input)) {
        // This means to remove the predicted prefix and
        // insert " * User ...".

        ForceLine(StringPrintf("* %s %s", user.c_str(), input.c_str()));
        return;
      } else if (Util::TryStripPrefix("/say ", &input)) {
        // This means to remove the predicted prefix and
        // insert " <User> ...".

        ForceLine(StringPrintf("<%s> %s", user.c_str(), input.c_str()));
        return;

      } else if (Util::TryStripPrefix("/undo", &input)) {
        // /undo N means discard the current line, and
        // remove N of the last chat lines.

        input = Util::NormalizeWhitespace(input);
        int num = input.empty() ? 1 : atoi(input.c_str());
        if (!lines.empty() && num > 0) {

          // Index of the element we rewind to.
          int new_size = std::max(0, (int)lines.size() - num);
          printf(AGREY("New size %d") "\n", new_size);
          for (int i = new_size; i < (int)lines.size(); i++) {
            printf(AGREY("Struck [%s]") "\n", lines[i].text.c_str());
          }

          // Reset to the beginning of the next line.
          CHECK(new_size <= (int)lines.size()) << "Bug";
          llm->LoadState(lines[new_size].state);
          start_line_state = std::move(lines[new_size].state);
          // And truncate everything after.
          lines.resize(new_size);

          if (!lines.empty()) {
            printf("%s\n", ANSILine(lines[lines.size() - 1]).c_str());
          }

          current_line.clear();
          // XXX: This should not be necessary, but state save/load
          // has a bug with a stale reference from matcher to parent nfa :(
          ResetRegex();

          // Don't predict the exact same thing!
          llm->NewRNG();
          return;
        }

      } else if (Util::TryStripPrefix("/pass", &input)) {
        // This means to force a different user to speak.

        input = Util::NormalizeWhitespace(input);

        std::vector<string> others =
          input.empty() ? GetOtherParticipants() : std::vector({input});
        std::string others_regex = MakeParticipantRegex(others) +
          // But we only exclude self for one line!
          // This may be unnecessary if we explicitly reset
          // after each line.
          MakeParticipantRegex(participants);

        printf(ABLUE("Regex: /%s/") "\n", others_regex.c_str());

        current_line.clear();
        llm->LoadState(start_line_state);
        // Might be wise to get new RNG since we previously rolled
        // the "wrong" dice.
        llm->NewRNG();
        // Here we're actually changing the regex to constrain the
        // output.
        llm->sampler.SetRegEx(others_regex);
        return;

      } else if (Util::TryStripPrefix("/save ", &input)) {
        // /save file

        input = Util::NormalizeWhitespace(input);
        if (input.empty()) input = "chat.txt";
        std::string contents;
        for (const ChatLine &line : lines) {
          if (line.is_action) {
            StringAppendF(&contents, " * %s %s\n",
                          line.participant.c_str(),
                          line.text.c_str());
          } else {
            StringAppendF(&contents, "<%s> %s\n",
                          line.participant.c_str(),
                          line.text.c_str());
          }
        }
        Util::WriteFile(input, contents);
        printf(AGREY("Wrote %s") "\n", input.c_str());
        // allow next command...

      } else if (Util::TryStripPrefix("/dump", &input)) {
        // /dump

        for (int idx = 0; idx < (int)lines.size(); idx++) {
          printf(AGREY("[%d]") " %s\n", idx,
                 ANSILine(lines[idx]).c_str());
        }

        // allow next command...

      } else {
        // Normal chat, continuing the prompt (whether it's <User> or * User).
        CHECK(!current_line.empty()) << "Bug";

        // If last token didn't include the space, insert it.
        if (current_line.back() != ' ') {
          input = " " + input;
        }
        input += "\n";

        llm->InsertString(input);

        current_line += input;

        FinishLine(current_line);
        return;
      }

      printf(ARED("REDO FROM START") "\n");
    }
  }

  void Chat() {

    for (;;) {
      if (Matches(user_nfa, current_line)) {
        DoUserInput();
      } else {

        // Is there pending input? If so, interrupt.
        // printf(AGREY("Has?"));
        if (console.HasInput()) {
          // printf(AGREY("Yes."));
          // XXX hax
          // Do we have enough text to bother interrupting?
          std::optional<ChatLine> oline = ParseLine(current_line);
          if (oline.has_value()) {
            printf(ARED("--") "\n");
            llm->InsertString("--\n");
            current_line += "--\n";
            FinishLine(current_line);
          } else {
            // Otherwise, remove this line.
            current_line.clear();
            llm->LoadState(start_line_state);
            ResetRegex();
            current_line = StringPrintf("<%s>", user.c_str());
            llm->InsertString(current_line);
            CHECK(Matches(user_nfa, current_line));
          }
        } else {
          // printf(AGREY("No."));
          // Get and commit a token.
          const int tok_id = llm->Sample();
          llm->TakeTokenBatch({tok_id});
          string tok = llm->context.TokenString(tok_id);
          current_line += tok;

          printf("%s", tok.c_str());
          fflush(stdout);

          if (tok_id == llm->context.NewlineToken()) {
            FinishLine(current_line);
          }
        }
      }
    }
  }

};


int main(int argc, char ** argv) {
  ANSI::Init();

  CHECK(argc >= 3) << "Usage: ./chat.exe User prompt.txt\n"
    "First line of the prompt file gives a list of participants,\n"
    "separated by commas.";

  string user = argv[1];
  string prompt = Util::ReadFile(argv[2]);
  CHECK(!prompt.empty()) << argv[2];
  std::vector<std::string> ppts = Util::Split(Util::getline(prompt), ',');
  CHECK(VectorContains(ppts, user)) << "The user " << user << " must be "
    "in the participants list. This is case-sensitive.";

  // AnsiInit();
  Timer model_timer;

  ContextParams cparams;
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q8_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  sparams.type = SampleType::MIROSTAT_2;
  // This is reset in chat.
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  printf(AGREEN("Loaded model") ".\n");

  Chatting chatting(&llm, ppts, user, prompt);
  chatting.Chat();

  return 0;
}
