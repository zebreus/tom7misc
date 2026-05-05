
#include "ansi.h"

#include <algorithm>
#include <cstdio>
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

  void SetTop(std::string_view s) {
    console->SetStatus(Console::TOP,
                       0,
                       ANSI_BG(0, 0, 80)
                       ANSI_FG(255, 255, 255)
                       "{}", s);
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
        "Think briefly. "
        "Task: Continue the conversation in a natural way. "
        "Respond with one or two messages, using IRC syntax:\n"
        "<{}> A message spoken aloud\n"
        " * {} takes an action.\n"
        "You play only the role{} of {}.\n",
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
            SetTop(std::format("Thinking... {}", thought_bytes));
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
            SetTop(ANSI_WHITE "...");
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

    SetTop(ANSI_GREEN "READY");

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

    } else if (Util::TryStripPrefix("/say ", &input)) {
      Message msg{
        .speaker = user,
        .text = std::string(input),
        .is_action = false
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

      Util::WriteFile(input, content);
      console->Print("\nWrote " AGREEN("{}") ".\n", input);
      return false;

    } else if (Util::TryStripPrefix("/pass", &input)) {
      // XXX take explicit participant
      return true;

    } else if (Util::TryStripPrefix("/verbose ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      verbosity = Util::ParseInt64(input, 1);
      return false;

      // TODO /kick

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

    console->SetStatus(Console::MID, 0,
                       ANSI_BG(30, 30, 30)
                       "─────────────────────────────────────────");

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
