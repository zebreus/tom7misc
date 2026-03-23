#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
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
#include "timer.h"
#include "tls-client.h"
#include "util.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static constexpr std::string_view API_HOST =
  "generativelanguage.googleapis.com";

[[maybe_unused]]
static constexpr std::string_view API_PATH_FLASH_LATEST =
  "gemini-3.1-pro-preview";

[[maybe_unused]]
static constexpr std::string_view MEDIUM_MODEL =
  "gemini-flash-latest";

[[maybe_unused]]
static constexpr std::string_view FASTEST_MODEL =
  "gemini-3.1-flash-lite-preview";

// XXX make command-line or whatever
static constexpr std::string_view MODEL = FASTEST_MODEL;

struct ModelClient {
  ModelClient(std::string_view host,
              std::string_view model_name,
              std::string_view api_key) : hostname(host),
                                          model_name(model_name),
                                          api_key(api_key) {
  }

  bool Connect() {
    const int port = 443;
    std::vector<Net::Address> addrs = Net::Resolve(API_HOST, port);

    if (addrs.empty()) {
      Print(stderr, ARED("Error") ": Couldn't resolve " ACYAN("{}") "\n",
            hostname);
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
      Print(stderr, ARED("Error") ": Couldn't connect to any address "
            "for " ACYAN("{}") "\n",
            hostname);
      return false;
    }

    client.reset(new TLSClient(std::move(sock), hostname));
    return client->DoHandshake();
  }

  int verbose = 2;

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

    CHECK(RE2::PartialMatch("   \"totalTokenCount\": 1234\n", pattern));
    CHECK(RE2::PartialMatch("   \"text\": \"This is it!\"\n", pattern));

    if (verbose > 1) {
      Print(stderr, "In ReadSomeJSON, read some HTTP.\n");
    }

    ReadSomeHTTP();

    // TODO: I think a cleaner thing would be to have a partial
    // JSON parser.

    {
      std::string_view s = http_content.StringView();
      // json_preparsed_to is the index before which we've already
      // ingested the data.
      CHECK(s.size() >= json_preparsed_to);
      s.remove_prefix(json_preparsed_to);
      std::string_view orig = s;
      if (verbose > 1) {
        Print(stderr, "JSON: {}\n", s);
      }

      std::string_view key, value;

      while (RE2::FindAndConsume(&s, pattern, &key, &value)) {
        Print(stderr, AYELLOW("{}") " = " AWHITE("{}") "\n",
              key, value);
        if (key == "text") {

          if (value.size() >= 2 &&
              value[0] == '\"' &&
              value.back() == '\"') {
            value.remove_prefix(1);
            value.remove_suffix(1);

            // XXX really want UnescapeJSON
            std::optional<std::vector<uint8_t>> unesc =
              Util::UnescapeC(value);

            if (!unesc.has_value()) {
              json_state = JSONState::BUSTED;
              return;
            }

            std::string_view sunesc =
              std::string_view((const char *)unesc.value().data(),
                               unesc.value().size());
            model_response.append(sunesc);
          } else {
            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "totalTokenCount") {
          if (std::optional<int64_t> tok = Util::ParseInt64(value)) {
            total_token_count = tok.value();

          } else {
            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "promptTokenCount") {
          if (std::optional<int64_t> tok = Util::ParseInt64(value)) {
            prompt_token_count = tok.value();

          } else {
            json_state = JSONState::BUSTED;
            return;
          }

        } else if (key == "finishReason") {
          json_state = JSONState::COMPLETED;
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

    client->ReadSome();

    if (verbose > 2) {
      std::string_view s = client->ReadView();
      Print(stderr, "In ReadSome, client data:\n{}\n", HexDump::Color(s));
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
        if (verbose > 0) {
          Print(stderr, ARED("Failed") ": {}\n", s);
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
          if (verbose > 2) {
            Print(stderr, AGREY("Length {} = {}; have {}") "\n",
                  hex, payload_size, packet.size());
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
      if (verbose > 0) {
        Print(stderr, AYELLOW("{}") ": " AWHITE("{}") "\n",
              key, value);
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


  std::string Infer(std::string_view prompt) {
    CHECK(client.get() != nullptr);

    std::string path =
      std::format("/v1beta/models/{}:streamGenerateContent", model_name);

    std::string payload =
      std::format(R"({{"contents": [{{"parts": [{{"text": "{}"}}]}}]}})",
                  prompt);

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
          hostname,
          api_key,
          payload.size(),
          payload);

    client->Send(request);

    // This is pretty hacky!
    // The streaming API returns chunks that are not proper
    // JSON, so we can't really "parse" them in any principled way.
    // We just append to the buffer according to the way that
    // the HTTP response is structured (e.g. Transfer-Encoding).

    size_t printed = 0;
    for (;;) {
      if (json_state == JSONState::COMPLETED) {
        return model_response;
      } else if (json_state == JSONState::BUSTED) {
        return "ERROR";
      } else if (json_state == JSONState::PARTIAL) {

        ReadSomeJSON();

        Print(stderr, AGREY("{}"), model_response.substr(printed));
        printed = model_response.size();
      }
    }
  }

 private:
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
};


int main(int argc, char* argv[]) {
  ANSI::Init();
  Net::Init();

  const std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());

  ModelClient client(API_HOST, MODEL, api_key);
  CHECK(client.Connect());

  // Read stdin.
  std::string prompt = Util::ReadStdin();
    /*
    "Can you tell me about an important fact you've discovered, that "
    "nobody seems to know about? I'm not talking about one of life's "
    "mysteries, or an underappreciated fact that is nonetheless "
    "documented, but a unique and new insight that you've had by "
    "observing documents and code on the internet. Like a bug in "
    "a piece of software, an accepted historical fact that can't "
    "be true because of contradictions with other facts, or evidence "
    "of wrongdoing. Use no more than 50 words.";
    */

  Timer response_timer;
  std::string result = client.Infer(prompt);
  double response_time = response_timer.Seconds();

  Print("{}\n", result);

  Print(stderr, "Response took {}\n", ANSI::Time(response_time));

  Net::Shutdown();
  return 0;
}
