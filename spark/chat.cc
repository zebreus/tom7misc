
#include "ansi.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "console.h"
#include "net.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "timer.h"
#include "util.h"

struct ReqMessage {
  std::string role;
  std::string content;
};

struct ModelRequest {
  std::vector<ReqMessage> messages;
};

struct ModelResponse {
  // The model's response.
  std::string content;
  // Optional thinking content.
  std::string reasoning_content;

  // If non-empty, then something went wrong and the content
  // may be absent.
  std::string error;
};


static ModelResponse Infer(const ModelRequest &req, bool verbose = false) {
  static constexpr std::string_view endpoint = "10.0.0.34";
  static constexpr int port = 8080;

  ModelResponse res;

  std::string payload = [&] -> std::string {
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      writer.StartObject();
      writer.Key("messages");
      writer.StartArray();
      for (const ReqMessage &msg : req.messages) {
        writer.StartObject();
        writer.Key("role");
        writer.String(msg.role.c_str());
        writer.Key("content");
        writer.String(msg.content.c_str());
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
      return buffer.GetString();
    }();

  if (verbose) {
    Print(ABLUE("{}") "\n", payload);
  }

  std::vector<Net::Address> addrs = Net::Resolve(endpoint, port);
  if (addrs.empty()) {
    res.error = "DNS resolution failed";
    return res;
  }

  Net::Socket sock = Net::Connect(addrs[0]);
  if (!sock.IsValid()) {
    res.error = "Connection failed";
    return res;
  }

  std::string http_req =
    std::format(
        "POST /v1/chat/completions HTTP/1.0\r\n"
        "Host: {}:{}\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: {}\r\n"
        "\r\n"
        "{}",
        endpoint, port, payload.size(),
        payload);

  if (!Net::SendAll(&sock, http_req)) {
    res.error = "Failed to send HTTP request";
    Net::Close(&sock);
    return res;
  }

  std::string http_resp;
  if (!Net::RecvAll(&sock, &http_resp)) {
    res.error = "Failed to receive HTTP response";
    Net::Close(&sock);
    return res;
  }
  Net::Close(&sock);

  size_t body_pos = http_resp.find("\r\n\r\n");
  if (body_pos == std::string::npos) {
    res.error = "Invalid HTTP response format";
    return res;
  }
  std::string body = http_resp.substr(body_pos + 4);

  rapidjson::Document d;
  d.Parse(body);
  if (d.HasParseError()) {
    res.error = "JSON parse error";
    return res;
  }

  if (!d.IsObject() || !d.HasMember("choices") || !d["choices"].IsArray() ||
      d["choices"].Empty()) {
    res.error = "Invalid or empty JSON response structure";
    return res;
  }

  const auto &choice = d["choices"][0];
  if (!choice.HasMember("message") || !choice["message"].IsObject()) {
    res.error = "No message found in the first choice";
    return res;
  }

  const auto &message = choice["message"];
  if (message.HasMember("content") && message["content"].IsString()) {
    res.content = message["content"].GetString();
  }

  if (message.HasMember("reasoning_content") &&
      message["reasoning_content"].IsString()) {
    res.reasoning_content = message["reasoning_content"].GetString();
  }

  return res;
}

[[maybe_unused]]
static void Test() {
  ModelRequest req{
    .messages = {
      ReqMessage{
        .role = "system",
        .content = "Answer precisely."
      },
      ReqMessage{
        .role = "user",
        .content = "What is a bash one-liner to see how many cores "
        "I have on a linux machine?",
      },
    },
  };

  Timer timer;
  ModelResponse res = Infer(req, true);
  Print("Got response in {}\n", ANSI::Time(timer.Seconds()));
  if (!res.error.empty()) {
    Print(ARED("{}") "\n", res.error);
  } else {
    if (!res.reasoning_content.empty()) {
      Print(AGREY("{}") "\n", res.reasoning_content);
    }

    Print("{}\n", res.content);
  }
}

struct Conversation {
  struct Message {
    std::string speaker;
    std::string text;
    bool is_action = false;
  };

  // 0: Only show the messages
  // 1: Shows sent data and thoughts
  // 2: Show debugging information
  int verbosity = 1;

  std::vector<Message> messages;
  std::string preamble = "Chat between Tom and his Computer.";

  // All participants.
  std::set<std::string> participants = {"Tom", "Computer"};

  std::set<std::string> user_participants = {"Tom"};

  void Load(std::string_view filename) {
    std::vector<std::string> lines = Util::ReadFileToLines(filename);
    if (lines.size() < 2) {
      Print(ARED("File not found / too short") ": {}\n", filename);
      return;
    }

    std::vector<std::string> ppts = Util::Split(lines[0], ',');
    if (ppts.size() < 2) {
      Print(ARED("First line should be comma-separated participants") ": {}\n",
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
      if (std::optional<Message> omsg = ParseMessage(line)) {
        messages.emplace_back(std::move(omsg.value()));
      } else {
        Print(AORANGE("Unparseable line") ": {}\n", line);
      }
    }

    Print(AGREEN("OK") "\n");
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

    req.messages.emplace_back(ReqMessage{
        .role = "system",
        .content = std::format(
            "Think briefly. "
            "Task: Continue the conversation in a natural way. "
            "Respond with one or two messages, using IRC syntax:\n"
            "<{}> A message spoken aloud\n"
            " * {} takes an action.\n"
            "You play only the role{} of {}.\n",
            example_speaker, example_speaker,
            robot_ppts.size() == 1 ? "" : "s",
            roles),
      });

    std::string transcript = preamble;
    transcript.push_back('\n');
    for (const Message &msg : messages) {
      if (msg.is_action) {
        AppendFormat(&transcript, " * {} {}\n", msg.speaker, msg.text);
      } else {
        AppendFormat(&transcript, "<{}> {}\n", msg.speaker, msg.text);
      }
    }

    req.messages.emplace_back(ReqMessage{
        .role = "user",
        .content = transcript,
      });

    ModelResponse resp = Infer(req, verbosity > 0);
    if (!resp.error.empty()) {
      // Just print the error and give control back to the user.
      Print(ARED("{}") "\n", resp.error);
      return;
    }

    if (!resp.reasoning_content.empty()) {
      if (verbosity > 0) {
        Print(AGREY("{}") "\n", resp.reasoning_content);
      }
    }

    for (std::string_view line : Util::SplitToLines(resp.content)) {
      if (std::optional<Message> omsg = ParseMessage(line)) {
        PrintMessage(omsg.value());
        messages.push_back(std::move(omsg.value()));
      } else {
        Print(ARED("Unable to parse: ") "{}\n", line);
      }
    }
  }

  std::optional<Message> ParseMessage(std::string_view sv) {
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
        msg.text = std::string(sv);
      } else {
        return std::nullopt;
      }
      return {msg};
    }

    return std::nullopt;
  }

  void PrintMessage(const Message &msg) {
    // TODO: Color etc.
    if (msg.is_action) {
      Print(" " AYELLOW("*") " " ACYAN("{}") " {}\n",
            msg.speaker, msg.text);
    } else {
      Print(AWHITE("<") ACYAN("{}") AWHITE(">") " {}\n",
            msg.speaker, msg.text);
    }
  }

  void PrintRecent(int n) {
    n = std::min((int)messages.size(), n);
    Print(AGREY("..."));
    for (int i = 0; i < n; i++) {
      PrintMessage(messages[messages.size() - 1 - i]);
    }
  }

  // True if we then pass to the opponent.
  bool DoOneUserInput(std::string_view input) {
    CHECK(!user_participants.empty());
    std::string user = *user_participants.begin();

    if (Util::TryStripPrefix("/raw ", &input) ||
        Util::TryStripPrefix("/force ", &input)) {

      if (std::optional<Message> omsg = ParseMessage(input)) {
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

    } else if (Util::TryStripPrefix("/pass", &input)) {
      // XXX take explicit participant
      return true;

    } else if (Util::TryStripPrefix("/verbose ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      verbosity = Util::ParseInt64(input, 1);
      return false;

    } else if (Util::TryStripPrefix("/invite ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      std::string target = std::string(input);

      if (participants.contains(target)) {
        Print(AORANGE("Already here") ": {}\n", target);
      } else {
        participants.insert(target);
        messages.emplace_back(Message{
            .speaker = target,
            .text = "joined.",
            .is_action = true,
          });
      }

      return false;

    } else if (Util::TryStripPrefix("/become ", &input)) {
      Util::RemoveOuterWhitespace(&input);
      std::string target = std::string(input);

      if (!participants.contains(target)) {
        Print(ARED("Error:") " unknown participant '{}'\n", target);
        return false;
      }

      user = target;
      user_participants.clear();
      user_participants.insert(user);

      // This doesn't imply passing.
      return false;

    } else {
      Print(ARED("REDO FROM START") "\n");
      return false;
    }
  }

  void Loop() {
    Console console;

    for (;;) {
      Print(AWHITE(":") AGREEN("> "));
      fflush(stdout);

      // True if we have a message (like /say) that implies
      // passing.
      bool pass = false;

      std::string input_line = console.WaitLine();

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
