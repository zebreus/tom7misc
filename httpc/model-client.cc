#include "model-client.h"

#include <cstdio>
#include <ctime>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "contiguous-buffer.h"
#include "hexdump.h"
#include "net.h"
#include "re2/re2.h"
#include "status-bar.h"
#include "timer.h"
#include "tls-client.h"
#include "utf8.h"
#include "util.h"

static constexpr bool SAVE_HTTP = true;

static constexpr std::string_view API_HOST =
  "generativelanguage.googleapis.com";

static std::string_view InternalModelName(Model model) {
  switch (model) {
  default:
    LOG(FATAL) << "Bad model?";
  case Model::GEMINI_BEST: return "gemini-3.1-pro-preview";
  case Model::GEMINI_MEDIUM: return "gemini-flash-latest";
  case Model::GEMINI_FASTEST: return "gemini-3.1-flash-lite-preview";
  case Model::GEMINI_CHEAPEST: return "gemini-flash-lite-latest";
  }
};

ModelClient::ModelClient() {}
ModelClient::~ModelClient() {}
ModelResponse::ModelResponse() {}
ModelResponse::~ModelResponse() {}

// To util?
static std::string EscapeJS(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c <= 0x1F) {
      out += std::format("\\u{:04x}", (uint8_t)c);
    } else {
      out += c;
    }
  }
  return out;
}

namespace {
// Individual call to the LLM (rest-style).
struct ModelConnection {
  ModelConnection(std::string_view host,
                  std::string_view model_name,
                  std::string_view api_key,
                  std::function<void(std::string_view)> Info) :
    hostname(host),
    model_name(model_name),
    api_key(api_key),
    Info(Info) {
  }

  bool Connect() {
    const int port = 443;
    std::vector<Net::Address> addrs = Net::Resolve(API_HOST, port);

    if (addrs.empty()) {
      if (Info) {
        Info(std::format(ARED("Error") ": Couldn't resolve " ACYAN("{}") "\n",
                         hostname));
      }

      json_state = JSONState::BUSTED;
      http_state = HTTPState::BUSTED;
      return false;
    }

    Net::Socket sock = [&]() {
        for (const Net::Address &addr : addrs) {
          Net::Socket sock = Net::Connect(addr);
          if (sock) return sock;
        }

        return Net::Socket{};
      }();
    if (!sock.IsValid()) {
      if (Info) {
        Info(std::format(ARED("Error") ": Couldn't connect to any address "
                         "for " ACYAN("{}") "\n",
                         hostname));
      }
      json_state = JSONState::BUSTED;
      http_state = HTTPState::BUSTED;
      return false;
    }

    client.reset(new TLSClient(std::move(sock), hostname));
    return client->DoHandshake();
  }

  void ReadSomeJSON() {
    CHECK(json_state != JSONState::COMPLETED);

    #define RE_IN_QUOTES R"_((?:[^"\\]|\\.))_"

    static RE2 pattern(
        "\"(text|totalTokenCount|finishReason)\"\\s*:\\s*"
        "("
        "(?:\\d+)"
        "|"
        "(?:\"" RE_IN_QUOTES "*\")"
        ")");

    CHECK(pattern.ok());

    DCHECK(RE2::PartialMatch("   \"totalTokenCount\": 1234\n", pattern));
    DCHECK(RE2::PartialMatch("   \"text\": \"This is it!\"\n", pattern));

    ReadSomeHTTP();

    // If we need more JSON content, but the underlying stream is
    // busted, then we are busted.
    if (http_state == HTTPState::BUSTED) {
      if (Info) {
        Info(std::format("JSON busted because HTTP busted. "
                         "Have {} with {} preparsed",
                         http_content.StringView().size(),
                         json_preparsed_to));
      }
      json_state = JSONState::BUSTED;
      return;
    }

    // TODO: I think a cleaner thing would be to have a partial
    // JSON parser.

    {
      std::string_view s = http_content.StringView();
      // json_preparsed_to is the index before which we've already
      // ingested the data.
      CHECK(s.size() >= json_preparsed_to);
      s.remove_prefix(json_preparsed_to);
      std::string_view orig = s;
      if (Info && verbose > 1) {
        Info(std::format("JSON: {}\n", s));
      }

      std::string_view key, value;

      while (RE2::FindAndConsume(&s, pattern, &key, &value)) {
        if (Info && verbose > 1) {
          Info(std::format(AYELLOW("{}") " = " AWHITE("{}") "\n",
                           key, value));
        }
        if (key == "text") {

          if (!sec_to_first.has_value()) {
            sec_to_first = {timer.Seconds()};
          }

          if (value.size() >= 2 &&
              value[0] == '\"' &&
              value.back() == '\"') {
            value.remove_prefix(1);
            value.remove_suffix(1);

            std::optional<std::string> unesc = Util::UnescapeJS(value);

            if (!unesc.has_value()) {
              if (Info) {
                Info(std::format("Could not unescape JSON: {}", value));
              }
              json_state = JSONState::BUSTED;
              return;
            }

            model_response.append(unesc.value());
          } else {
            if (Info) {
              Info(std::format("Expected quoted string for text, but got: {}",
                               value));
            }
            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "totalTokenCount") {
          if (std::optional<int64_t> tok = Util::ParseInt64(value)) {
            total_token_count = tok.value();

          } else {
            if (Info) {
              Info(std::format("Expected int for totalTokenCount: {}",
                               value));
            }

            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "promptTokenCount") {
          if (std::optional<int64_t> tok = Util::ParseInt64(value)) {
            prompt_token_count = tok.value();

          } else {
            if (Info) {
              Info(std::format("Expected int for promptTokenCount: {}",
                               value));
            }

            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "finishReason") {
          json_state = JSONState::COMPLETED;

          sec_to_complete = {timer.Seconds()};
          break;

        } else {
          LOG(FATAL) << "Bug!";
        }
      }

      // Parsed to this point.
      size_t consumed = s.data() - orig.data();
      json_preparsed_to += consumed;
    }
  }


  // Read data to pump the http state machine, or block until
  // some is available.
  void ReadSomeHTTP() {
    // We must be connected and not already done.
    CHECK(client.get() != nullptr);
    CHECK(!client->ReadEOS());

    if (Info && verbose > 2) {
      size_t bytes = client->ReadView().size();
      Info(std::format("ReadSomeHTTP (have {} so far)...\n", bytes));
    }

    client->ReadSome();

    if (Info && verbose > 2) {
      std::string_view s = client->ReadView();
      Info(std::format("In ReadSome, client data:\n{}\n", HexDump::Color(s)));
    }

    while (http_state == HTTPState::STATUS) {
      std::string_view s = client->ReadView();
      auto eol = s.find("\r\n");
      if (eol == std::string_view::npos)
        return;

      if (s.find("200 OK") != std::string_view::npos) {
        client->RemovePrefix(eol + 2);
        http_state = HTTPState::HEADERS;
      } else {
        if (Info) {
          Info(std::format(ARED("Failed") ": {}\n", s));
        }
        http_state = HTTPState::BUSTED;
        break;
      }
    }

    // Advance our HTTP state machine.
    while (http_state == HTTPState::HEADERS) {
      // This means we're looking at the beginning of a header line.
      std::string_view s = client->ReadView();
      auto eol = s.find("\r\n");
      if (eol == std::string_view::npos)
        return;

      std::string_view hdr = s.substr(0, eol);
      client->RemovePrefix(eol + 2);
      if (hdr.empty()) {
        if (ProcessHeaders()) {
          http_state = HTTPState::BODY;
        } else {
          http_state = HTTPState::BUSTED;
        }
      } else {
        auto colon = hdr.find(':');
        if (colon == std::string_view::npos) {
          if (Info) {
            Info(std::format(ARED("Bad header") ": {}\n", hdr));
          }
          http_state = HTTPState::BUSTED;
          break;
        }

        std::string_view key = hdr.substr(0, colon);
        std::string_view value = hdr.substr(colon + 1);
        Util::RemoveOuterWhitespace(&key);
        Util::RemoveOuterWhitespace(&value);
        headers.emplace_back(key, value);
      }
    }

    while (http_state == HTTPState::BODY) {
      if (transfer_chunked) {
        std::string_view s = client->ReadView();
        auto eol = s.find("\r\n");
        if (eol == std::string::npos) {
          // Don't even have length yet.
          break;
        }

        std::string_view hex = s.substr(0, eol);
        std::string_view packet = s.substr(eol + 2);
        if (std::optional<uint64_t> olen = Util::ParseHex(hex)) {
          const size_t payload_size = olen.value();
          if (Info && verbose > 2) {
            Info(std::format(AGREY("Length {} = {}; have {}") "\n",
                             hex, payload_size, packet.size()));
          }
          // Also need to read the trailing \r\n.
          size_t take_size = payload_size + 2;
          if (packet.size() >= take_size) {
            http_content.Append(packet.substr(0, payload_size));
            // Consume length\r\npacket\r\n.
            client->RemovePrefix(eol + 2 + payload_size + 2);
          } else {
            // Not enough data yet.
            break;
          }

          if (payload_size == 0) {
            // This is how the end of stream is marked.
            http_state = HTTPState::COMPLETED;
            break;
          }

        } else {
          http_state = HTTPState::BUSTED;
          break;
        }
      } else {
        std::span<const uint8_t> s = client->ReadSpan();
        if (s.empty() && client->ReadEOS()) {
          http_state = HTTPState::COMPLETED;
          break;
        }

        // Any bytes are just content.
        http_content.Append(client->ReadSpan());
        client->ClearReadBuffer();
        break;
      }
    }
  }

  bool ProcessHeaders() {
    for (const auto &[key, value] : headers) {
      if (Info && verbose > 1) {
        Info(std::format(AYELLOW("{}") ": " AWHITE("{}") "\n",
                         key, value));
      }
      if (Util::lcase(key) == "transfer-encoding") {
        if (Util::lcase(value) != "chunked") {
          return false;
        }

        transfer_chunked = true;
      }
    }

    return true;
  }

  void SetVerbose(int v) {
    verbose = v;
  }

  std::string hostname;
  std::string model_name;
  std::string api_key;
  std::unique_ptr<TLSClient> client;

  // API response stuff.
  enum class JSONState { PARTIAL, BUSTED, COMPLETED, };
  JSONState json_state = JSONState::PARTIAL;
  size_t json_preparsed_to = 0;
  ContiguousBuffer json;
  std::string model_response;
  int64_t prompt_token_count = 0;
  int64_t total_token_count = 0;

  // State describing the HTTP response.
  enum class HTTPState { STATUS, HEADERS, BODY, BUSTED, COMPLETED, };
  HTTPState http_state = HTTPState::STATUS;
  // If true, then we have transfer-encoding: chunked.
  bool transfer_chunked = false;
  std::vector<std::pair<std::string, std::string>> headers;
  // The content of the HTTP response.
  ContiguousBuffer http_content;

  Timer timer;
  std::optional<double> sec_to_first, sec_to_complete;

  int verbose = 0;
  std::function<void(std::string_view)> Info;
};

struct ModelResponseImpl : public ModelResponse {
  ModelResponseImpl(std::string_view host,
                    std::string_view model_name,
                    std::string_view api_key,
                    std::function<void(std::string_view)> Info) :
    hostname(host),
    model_name(model_name),
    api_key(api_key) {
    conn.reset(new ModelConnection(host, model_name, api_key, Info));
    // XXX handle failure
    (void)conn->Connect();
  }

  void Send(std::string_view http_request) {
    CHECK(conn.get() != nullptr);
    conn->client->Send(http_request);
  }

  bool Failed() const override {
    return conn.get() == nullptr ||
      conn->json_state == ModelConnection::JSONState::BUSTED;
  }

  bool Completed() const override {
    return conn.get() != nullptr &&
      conn->json_state == ModelConnection::JSONState::COMPLETED;
  }

  // Block until we make progress.
  void ReadSome() override {
    CHECK(!Failed() && !Completed());
    CHECK(conn.get() != nullptr);
    conn->ReadSomeJSON();
  }

  // Block until we are completed or failed.
  void ReadAll() override {
    if (Failed() || Completed()) return;
    CHECK(conn.get() != nullptr);

    while (!Failed() && !Completed()) {
      ReadSome();
    }
  }

  std::string_view Text() const override {
    if (conn.get() == nullptr) return "";
    return conn->model_response;
  }

  int64_t TotalTokens() const override {
    if (conn.get() == nullptr) return 0;
    return conn->total_token_count;
  }

  int64_t PromptTokens() const override {
    if (conn.get() == nullptr) return 0;
    return conn->prompt_token_count;
  }


  // Seconds to the first token from the model (or the current time, if
  // it hasn't happened yet.)
  double SecToFirst() const override {
    if (conn.get() == nullptr) return 0.0;
    if (conn->sec_to_first.has_value())
      return conn->sec_to_first.value();
    return conn->timer.Seconds();
  }

  double Sec() const override {
    if (conn.get() == nullptr) return 0.0;
    if (conn->sec_to_complete.has_value())
      return conn->sec_to_complete.value();
    return conn->timer.Seconds();
  }


  ~ModelResponseImpl() override {

  }

  void SetVerbose(int v) {
    verbose = v;
    if (conn.get() != nullptr) {
      conn->SetVerbose(verbose);
    }
  }

  // This is mostly just a light wrapper around a connection.
  std::unique_ptr<ModelConnection> conn;
  int verbose = 0;

  // Save these for future error reporting, retries.
  std::string hostname;
  std::string model_name;
  std::string api_key;
};

struct ModelClientImpl : public ModelClient {
  ModelClientImpl(Model model, std::string_view api_key) :
    model_name(InternalModelName(model)), api_key(api_key) {
  }

  std::string model_name;
  std::string api_key;
  int verbose = 1;

  void SetVerbose(int v) override {
    verbose = v;
  }

  std::unique_ptr<ModelResponse> Run(std::string_view prompt) override {
    return RunInternal(prompt, [](std::string_view s) {
        Print(stderr, "{}", s);
      });
  }

  std::unique_ptr<ModelResponseImpl> RunInternal(
      std::string_view prompt,
      std::function<void(std::string_view)> Info) {

    std::unique_ptr<ModelResponseImpl> resp(
        new ModelResponseImpl(API_HOST, model_name, api_key, Info));
    resp->SetVerbose(verbose);

    std::string path =
      std::format("/v1beta/models/{}:streamGenerateContent", model_name);

    std::string payload =
      std::format(R"({{"contents": [{{"parts": [{{"text": "{}"}}]}}]}})",
                  EscapeJS(prompt));

    std::string request =
      std::format(
          "POST {} HTTP/1.1\r\n"
          "Host: {}\r\n"
          "User-Agent: aget.cc\r\n"
          "Accept: application/json\r\n"
          "Content-Type: application/json\r\n"
          "X-goog-api-key: {}\r\n"
          "Content-Length: {}\r\n"
          "\r\n"
          "{}",
          path,
          API_HOST,
          api_key,
          payload.size(),
          payload);

    resp->Send(request);

    // Upcast
    return resp;
  }

  std::string Infer(std::string_view prompt) override {
    std::unique_ptr<StatusBar> status;

    if (verbose > 0) {
      status.reset(new StatusBar(3));
    }

    std::unique_ptr<ModelResponseImpl> resp =
      RunInternal(prompt,
                  [status = status.get()](std::string_view s) {
                    if (status) {
                      status->Emit(s);
                    }
                  });

    #define PROMPT_COLOR ANSI_FG(138, 188, 242)
    #define RESP_COLOR ANSI_FG(207, 138, 242)
    #define MARK_COLOR ANSI_FG(30, 30, 35)

    std::string prompt_line =
      std::format(PROMPT_COLOR "{}" ANSI_RESET,
                  Util::Replace(UTF8::Truncate(prompt, 75),
                                "\n", ANSI_GREY "¶" PROMPT_COLOR));

    std::string result = "ERROR";
    for (;;) {
      if (status.get() != nullptr && verbose > 2) {
        status->Print("Start status loop.\n");
      }
      if (resp->Completed()) {
        result = std::string(resp->Text());
        break;
      } else if (resp->Failed()) {
        result = "ERROR";
        break;
      } else {
        if (status.get() != nullptr) {
          std::string stats =
            std::format("TTF {} | {} prompt | {} total | {}",
                        ANSI::Time(resp->SecToFirst()),
                        resp->PromptTokens(),
                        resp->TotalTokens(),
                        ANSI::Time(resp->Sec()));


          std::string r = UTF8::RTruncate(resp->Text(), 75);

          std::string resp_line =
            std::format(RESP_COLOR "{}" ANSI_RESET,
                        Util::Replace(r,
                                      "\n", MARK_COLOR "¶" RESP_COLOR));

          status->EmitStatus({stats, prompt_line, resp_line});
        }

        resp->ReadSome();
      }
    }

    if (status.get() != nullptr) {
      status->Remove();
    }
    if (SAVE_HTTP) {
      if (resp.get() && resp->conn.get()) {
        std::string filename = std::format("http.{}.txt",
                                           time(nullptr));
        Util::WriteFile(filename, resp->conn->http_content.StringView());
        Print("Wrote http response to " AGREEN("{}") "\n", filename);
      } else {
        Print(ARED("No http response to save") "?\n");
      }
    }

    return result;
  }
};

}  // namespace


std::unique_ptr<ModelClient> ModelClient::Create(
      Model model,
      std::string_view api_key) {
  return std::unique_ptr<ModelClient>{new ModelClientImpl(model, api_key)};
}

