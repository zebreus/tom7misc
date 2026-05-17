
#include "ansi.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "console.h"
#include "model-util.h"
#include "net.h"
#include "periodically.h"
#include "spark-infer.h"
#include "util.h"

static constexpr std::string_view HOST = "10.0.0.34";
static constexpr int PORT = 8080;

using ReqMessage = Spark::ReqMessage;
using ModelRequest = Spark::ModelRequest;
using ModelResponse = Spark::ModelResponse;
using SMR = Spark::StreamingModelResponse;

struct Conversation {
  Conversation() : spark(HOST, PORT) {
    console = std::make_unique<Console>(1, 1000, 1, 0);
  }

  struct Message {
    std::string speaker;
    std::string text;
    bool is_action = false;
  };

  static std::string MessageString(const Message &msg) {
    if (msg.is_action) {
      return std::format(" * {} {}", msg.speaker, msg.text);
    } else {
      return std::format("<{}> {}", msg.speaker, msg.text);
    }
  }

  Spark spark;

  // 0: Only show the messages
  // 1: Shows sent data and thoughts
  // 2: Show debugging information
  int verbosity = 0;

  std::vector<Message> messages;
  std::string preamble = "Chat between Tom and his Computer.";

  // All participants.
  std::set<std::string> participants = {"Tom", "Computer"};

  std::set<std::string> user_participants = {"Tom"};

  void ShowParticipants() {
    std::string ppts = ANSI_BG(0, 0, 80);
    bool first = true;
    for (const std::string &s : user_participants) {
      if (!first) AppendFormat(&ppts, ANSI_YELLOW " ⏹ ");
      AppendFormat(&ppts, ANSI_WHITE "[" ANSI_CYAN "{}" ANSI_WHITE "]", s);
      first = false;
    }

    for (const std::string &s : participants) {
      if (!user_participants.contains(s)) {
        if (!first) AppendFormat(&ppts, ANSI_YELLOW " ⏹ ");
        AppendFormat(
            &ppts,
            ANSI_WHITE "[" ANSI_FG(200, 200, 200) "{}" ANSI_WHITE "]", s);
        first = false;
      }
    }

    console->SetStatus(Console::TOP, 0, "{}", ppts);
  }

  void SetMiddle(std::string_view message) {
    console->SetStatus(Console::MID, 0,
                       ANSI_FG(100, 100, 100)
                       ANSI_BG(30, 30, 30)
                       "────┤"
                       " {} "
                       ANSI_FG(100, 100, 100)
                       ANSI_BG(30, 30, 30) "├──────────────────────────",
                       message);
  }

  void Load(std::string_view filename) {
    std::vector<std::string> lines = Util::ReadFileToLines(filename);
    if (lines.size() < 2) {
      console->Print(ARED("File not found / too short") ": {}\n", filename);
      return;
    }

    std::vector<std::string> ppts = Util::Split(lines[0], ',');
    if (ppts.size() < 2) {
      console->Print(ARED("First line should be comma-separated "
                          "participants") ": {}\n",
                     lines[0]);
    }
    participants.clear();

    user_participants = {ppts[0]};
    for (int i = 1; i < ppts.size(); i++) {
      participants.insert(ppts[i]);
    }
    ShowParticipants();

    // Next line is preamble.
    preamble = lines[1];
    for (int i = 2; i < (int)lines.size(); i++) {
      std::string_view line = lines[i];
      Util::RemoveOuterWhitespace(&line);
      if (line.empty()) continue;
      if (std::optional<Message> omsg = ParseMessage(line, false)) {
        messages.emplace_back(std::move(omsg.value()));
      } else {
        console->Print(AORANGE("Unparseable line") ": {}\n", line);
      }
    }

    console->Print(AGREEN("OK") "\n");
  }

  std::string CondensePreamble() {
    ModelRequest req;

    req.instructions =
      "Task: Produce a condensed synopsis of a partial chat transcript. "
      "Your synopsis should capture the entire conversation or story, some "
      "of which occurred before or after this partial chat. Use the "
      "existing synopsis, combined with the portion you see, to produce "
      "an updated synopsis. The new synopsis should be no more than 1000 "
      "words. "
      "Include information that would be important for maintaining "
      "continuity and tone.\n\n";

    std::string input =
      "The existing synopsis and transcript appears next:\n"
      "<TRANSCRIPT>\n";

    input.append(preamble);
    input.push_back('\n');
    for (const Message &msg : messages) {
      AppendFormat(&input, "{}\n", MessageString(msg));
    }
    input.append("</TRANSCRIPT>\n");

    input.append("\nNow, please provide a plain text, dense synopsis, "
                 "of up to 1000 words:\n");

    req.messages.emplace_back(ReqMessage{
        .role = "user",
        .content = input,
      });

    std::unique_ptr<SMR> res = spark.Stream(req, verbosity);

    int64_t thought_bytes = 0;
    Periodically status_per(0.250);

    auto GotToken = [&](std::string_view what, std::string_view tok) {
        thought_bytes += tok.size();
        status_per.RunIf([&]{
            SetMiddle(std::format("{}... {}", what, thought_bytes));
          });
      };

    using namespace std::chrono_literals;
    using SMR = Spark::StreamingModelResponse;

    for (;;) {
      auto p = res->Poll();
      if (SMR::Thought *t = std::get_if<SMR::Thought>(&p)) {
        GotToken("thinking", t->tok);
      } else if (SMR::Content *c = std::get_if<SMR::Content>(&p)) {
        GotToken("writing", c->tok);
      } else if (SMR::Error *e = std::get_if<SMR::Error>(&p)) {
        // Just print the error and give control back to the user.
        console->Print(ARED("{}") "\n", e->msg);
        return preamble;
      } else if (std::holds_alternative<SMR::Wait>(p)) {
        std::this_thread::sleep_for(100ms);
      } else if (std::holds_alternative<SMR::Done>(p)) {
        break;
      }
    }

    std::string content = res->FullContent();
    SetMiddle(ANSI_GREEN "READY");

    return Util::NormalizeWhitespace(content);
  }

  // Get at least one continuation line, synchronously.
  void Continue() {
    ModelRequest req;

    std::vector<std::string> robot_ppts;
    for (const std::string &ppt : participants) {
      if (!user_participants.contains(ppt)) {
        robot_ppts.push_back(ppt);
      }
    }

    std::string example_speaker = "Speaker";
    if (robot_ppts.size() == 1)
      example_speaker = robot_ppts[0];

    std::string roles = Util::Join(robot_ppts, ", ");
    req.instructions = std::format(
        // "SPECIAL INSTRUCTION: Think silently. Thinking budget: 32 tokens\n"
        "Task: Continue the conversation in a natural way. "
        "Respond with one or two messages, using IRC syntax:\n"
        "<{}> A message spoken aloud\n"
        " * {} takes an action.\n"
        "Both types of messages are seen by the chat room. "
        "Respond only as the user{} {}.\n",
        example_speaker, example_speaker,
        robot_ppts.size() == 1 ? "" : "s",
        roles);

    std::string transcript = preamble;
    transcript.push_back('\n');
    for (const Message &msg : messages) {
      AppendFormat(&transcript, "{}\n", MessageString(msg));
    }

    req.messages.emplace_back(ReqMessage{
        .role = "user",
        .content = transcript,
      });

    std::unique_ptr<SMR> res = spark.Stream(req, verbosity);

    int64_t thought_bytes = 0;
    std::string content;
    Periodically status_per(0.250);
      /*
        could show this if verbose is on
            if (!resp.reasoning_content.empty()) {
      if (verbosity > 0) {
        Print(AGREY("{}") "\n", resp.reasoning_content);
      }
      */

    auto ThoughtToken = [&](std::string_view tok) {
        thought_bytes += tok.size();
        status_per.RunIf([&]{
            SetMiddle(std::format("Thinking... {}", thought_bytes));
          });
      };

    auto ConsumeLine = [&](std::string_view line) {
        Util::RemoveLeadingWhitespace(&line);
        Util::RemoveTrailingWhitespace(&line);
        if (line.empty())
          return;
        if (std::optional<Message> omsg = ParseMessage(line, true)) {
          PrintMessage(omsg.value());
          messages.push_back(std::move(omsg.value()));
        } else {
          console->Print(ARED("Unable to parse: ") "{}\n", line);
        }
      };

    auto ContentToken = [&](std::string_view tok) {
        status_per.RunIf([&]{
            SetMiddle(ANSI_WHITE "...");
          });
        content.append(tok);

        for (;;) {
          size_t pos = content.find('\n');
          if (pos == std::string::npos)
            return;

          std::string line = content.substr(0, pos);
          content = content.substr(pos + 1);
          ConsumeLine(line);
        }
      };

    using namespace std::chrono_literals;
    using SMR = Spark::StreamingModelResponse;

    for (;;) {
      auto p = res->Poll();
      if (SMR::Thought *t = std::get_if<SMR::Thought>(&p)) {
        ThoughtToken(t->tok);
      } else if (SMR::Content *c = std::get_if<SMR::Content>(&p)) {
        ContentToken(c->tok);
      } else if (SMR::Error *e = std::get_if<SMR::Error>(&p)) {
        // Just print the error and give control back to the user.
        console->Print(ARED("{}") "\n", e->msg);
        return;
      } else if (std::holds_alternative<SMR::Wait>(p)) {
        std::this_thread::sleep_for(100ms);
      } else if (std::holds_alternative<SMR::Done>(p)) {
        break;
      }
    }

    SetMiddle(ANSI_GREEN "READY");

    // If it did not have a trailing newline, say.
    ConsumeLine(content);
  }

  std::optional<Message> ParseMessage(std::string_view sv, bool sloppy) {
    Util::RemoveOuterWhitespace(&sv);
    if (sv.empty()) return std::nullopt;

    Message msg;
    if (Util::TryStripPrefix("*", &sv)) {
      // Allow no space after *; nonstandard.
      Util::RemoveLeadingWhitespace(&sv);
      msg.is_action = true;
      size_t space = sv.find(' ');
      if (space != std::string_view::npos) {
        msg.speaker = std::string(sv.substr(0, space));
        sv.remove_prefix(space);
        Util::RemoveLeadingWhitespace(&sv);
        msg.text = std::string(sv);
      } else {
        msg.speaker = std::string(sv);
      }
      return {msg};

    } else if (Util::TryStripPrefix("<", &sv)) {
      msg.is_action = false;
      size_t rangle = sv.find('>');
      if (rangle != std::string_view::npos) {
        msg.speaker = std::string(sv.substr(0, rangle));
        sv.remove_prefix(rangle + 1);
        Util::RemoveLeadingWhitespace(&sv);

        if (sloppy) {
          // Allow <Tom> *wrings his hands*
          if (sv.size() > 2 && sv.starts_with("*") &&
              sv.ends_with("*")) {
            msg.is_action = true;
            sv.remove_prefix(1);
            sv.remove_suffix(1);
          }
        }
        msg.text = std::string(sv);
      } else {
        return std::nullopt;
      }
      return {msg};
    }

    return std::nullopt;
  }

  std::optional<std::string> GetOther() {
    std::vector<std::string> robot_ppts;
    for (const std::string &ppt : participants) {
      if (!user_participants.contains(ppt)) {
        robot_ppts.push_back(ppt);
      }
    }
    if (robot_ppts.size() == 1) {
      return {robot_ppts[0]};
    } else {
      return std::nullopt;
    }
  }

  void PrintMessage(const Message &msg) {
    if (msg.is_action) {
      console->Print(" " AYELLOW("*") " " ACYAN("{}") " {}\n",
                     msg.speaker, msg.text);
    } else {
      console->Print(AWHITE("<") ACYAN("{}") AWHITE(">") " {}\n",
                     msg.speaker, msg.text);
    }
  }

  void PrintRecent(int n) {
    n = std::min((int)messages.size(), n);
    console->Print(AGREY("...") "\n");
    for (int i = 0; i < n; i++) {
      PrintMessage(messages[messages.size() - n + i]);
    }
  }

  // True if we then pass to the opponent.
  bool DoOneUserInput(std::string_view input) {
    CHECK(!user_participants.empty());
    std::string user = *user_participants.begin();

    if (Util::TryStripPrefix("/raw ", &input) ||
        Util::TryStripPrefix("/force ", &input)) {

      if (std::optional<Message> omsg = ParseMessage(input, false)) {
        PrintMessage(omsg.value());
        messages.push_back(std::move(omsg.value()));
      }

      return true;

    } else if (Util::TryStripPrefix("/me ", &input)) {
      Message msg{
        .speaker = user,
        .text = std::string(input),
        .is_action = true
      };
      PrintMessage(msg);
      messages.push_back(std::move(msg));
      return true;

    } else if (Util::TryStripPrefix("/you ", &input)) {
      if (std::optional<std::string> other = GetOther()) {
        Message msg{
          .speaker = other.value(),
          .text = std::string(input),
          .is_action = true,
        };
        PrintMessage(msg);
        messages.push_back(std::move(msg));
      } else {
        console->Print(AORANGE("/you") " only when exactly one other "
                       "participant.\n");
      }
      return false;

    } else if (Util::TryStripPrefix("/hear ", &input)) {
      if (std::optional<std::string> other = GetOther()) {
        Message msg{
          .speaker = other.value(),
          .text = std::string(input),
          .is_action = false,
        };
        PrintMessage(msg);
        messages.push_back(std::move(msg));
      } else {
        console->Print(AORANGE("/hear") " only when exactly one other "
                       "participant.\n");
      }
      return false;

    } else if (Util::TryStripPrefix("/say ", &input)) {
      Message msg{
        .speaker = user,
        .text = std::string(input),
        .is_action = false,
      };
      PrintMessage(msg);
      messages.push_back(std::move(msg));
      return true;

    } else if (Util::TryStripPrefix("/undo", &input)) {
      Util::RemoveOuterWhitespace(&input);
      int num = Util::ParseInt64(input, 1);

      if (messages.size() > num) {
        messages.resize(messages.size() - num);
      } else {
        messages.clear();
      }

      PrintRecent(3);
      return false;

    } else if (Util::TryStripPrefix("/load", &input)) {
      Util::RemoveOuterWhitespace(&input);
      Load(input);

      PrintRecent(8);

      return false;

    } else if (Util::TryStripPrefix("/dump", &input)) {
      PrintRecent(messages.size());
      return false;

    } else if (Util::TryStripPrefix("/save ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      namespace fs = std::filesystem;
      fs::path filename = ModelUtil::NormalizePath(input);
      switch (fs::status(filename).type()) {
      case fs::file_type::not_found:
      case fs::file_type::regular:
        // ok.
        break;
      default:
        console->Print(AORANGE("{}") " exists and is not "
                       "a regular file!\n", filename.string());
        return false;
      }

      std::string content;
      for (std::string_view up : user_participants) {
        if (!content.empty()) content.append(",");
        content.append(up);
      }
      for (const std::string &ppt : participants) {
        if (!user_participants.contains(ppt)) {
          if (!content.empty()) content.append(",");
          content.append(ppt);
        }
      }

      AppendFormat(&content, "\n{}\n", preamble);
      for (const Message &msg : messages) {
        AppendFormat(&content, "{}\n", MessageString(msg));
      }

      Util::WriteFile(filename.string(), content);
      console->Print("\nWrote " AGREEN("{}") ".\n", filename.string());
      return false;

    } else if (Util::TryStripPrefix("/preamble", &input)) {
      Util::RemoveOuterWhitespace(&input);
      if (input.empty()) {
        // If we have no input, this command means to
        // edit the preamble, which we do by putting it
        // back in the command.

        console->SetInput(std::format("/preamble {}", preamble));
      } else {
        preamble = input;
        console->Print("Preamble is now " AGREY("{}") "\n",
                       preamble);
      }
      return false;

    } else if (Util::TryStripPrefix("/condense", &input)) {
      std::string new_preamble = CondensePreamble();
      console->SetInput(std::format("/preamble {}", new_preamble));
      // Keep only the last 50 messages.
      constexpr int CONDENSE_TARGET = 50;
      if (messages.size() > CONDENSE_TARGET) {
        messages.erase(messages.begin(), messages.end() - CONDENSE_TARGET);
      }
      return false;

    } else if (Util::TryStripPrefix("/pass", &input)) {
      // XXX take explicit participant
      return true;

    } else if (Util::TryStripPrefix("/verbose ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      verbosity = Util::ParseInt64(input, 1);
      return false;

    } else if (Util::TryStripPrefix("/kick ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      std::string target = std::string(input);

      if (!participants.contains(target)) {
        console->Print(AORANGE("Not here") ": {}\n", target);
      } else if (participants.size() == 1) {
        console->Print(AORANGE("Can't kick the last participant") ": {}\n",
                       target);
      } else {
        participants.erase(target);
        user_participants.erase(target);
        messages.emplace_back(Message{
            .speaker = target,
            .text = "left.",
            .is_action = true,
          });
        if (target == user) {
          user = *participants.begin();
        }
      }

      ShowParticipants();

      return false;

    } else if (Util::TryStripPrefix("/invite ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      std::string target = std::string(input);

      if (participants.contains(target)) {
        console->Print(AORANGE("Already here") ": {}\n", target);
      } else {
        participants.insert(target);
        messages.emplace_back(Message{
            .speaker = target,
            .text = "joined.",
            .is_action = true,
          });
      }

      ShowParticipants();

      return false;

    } else if (Util::TryStripPrefix("/reset", &input)) {
      messages.clear();
      preamble = "";
      Util::RemoveOuterWhitespace(&input);
      if (!input.empty()) {
        // Reset participants too.
        std::vector<std::string> ppts = Util::Split(input, ',');
        for (std::string &ppt : ppts) {
          ppt = Util::LoseWhiteL(Util::LoseWhiteR(ppt));
        }

        if (!ppts.empty() && !ppts[0].empty()) {
          participants.clear();
          user_participants = {ppts[0]};
          console->Print("[Reset. You are {}]\n", ppts[0]);
          for (int i = 1; i < ppts.size(); i++) {
            participants.insert(ppts[i]);
            console->Print(" * {} joined\n", ppts[i]);
          }
        }
      }

      ShowParticipants();

      return false;

    } else if (Util::TryStripPrefix("/become ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      std::string target = std::string(input);

      if (!participants.contains(target)) {
        console->Print(ARED("Error:") " unknown participant '{}'\n", target);
        return false;
      }

      user = target;
      user_participants.clear();
      user_participants.insert(user);

      ShowParticipants();

      // This doesn't imply passing.
      return false;

    } else {
      console->Print(ARED("REDO FROM START") "\n");
      return false;
    }
  }

  std::unique_ptr<Console> console;

  void Loop() {

    console->SetStatus(Console::TOP, 0,
                       ANSI_BG(0, 0, 80) "Chat");

    SetMiddle("");

    console->Redraw();

    for (;;) {
      // Print(AWHITE(":") AGREEN("> "));
      // fflush(stdout);

      // True if we have a message (like /say) that implies
      // passing.
      bool pass = false;

      std::string input_line = console->WaitLine();

      std::vector<std::string> raw_inputs =
        Util::SplitWith(input_line, "\\n");
      CHECK(!raw_inputs.empty());

      std::vector<std::string> inputs;
      for (int i = 0; i < raw_inputs.size(); i++) {
        std::string_view input = raw_inputs[i];
        Util::RemoveOuterWhitespace(&input);
        if (input.empty()) {
          continue;
        }

        if (input[0] == '/') {
          inputs.emplace_back(input);
        } else {
          inputs.emplace_back(std::format("/say {}", input));
        }
      }

      for (std::string_view input : inputs) {
        pass = DoOneUserInput(input) || pass;
      }

      if (pass) {
        Continue();
      }
    }
  }

};

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  // Test();
  Conversation convo;
  convo.Loop();

  return 0;
}
