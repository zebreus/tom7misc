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
#include "net.h"
#include "timer.h"
#include "tls-client.h"
#include "util.h"
#include "hexdump.h"

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

  int verbose = 1;

  // Read data to pump the http state machine, or block until
  // some is available.
  void ReadSome() {
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
          if (verbose > 2) {
            Print(stderr, AGREY("Length {} = {}; have {}") "\n",
                  hex, olen.value(), packet.size());
          }
          // Also need to read the trailing \r\n.
          size_t size = olen.value() + 2;
          if (packet.size() >= size) {
            content.Append(packet.substr(0, size));
            // Consume length\r\npacket\r\n.
            client->RemovePrefix(eol + 2 + size);
          }

          if (olen.value() == 0) {
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
        content.Append(client->ReadSpan());
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

    for (;;) {
      if (http_state == HTTPState::COMPLETED) {
        // XXX parse json to get *actual* result.
        return std::string(content.StringView());
      } else if (http_state == HTTPState::BUSTED) {
        // Return partial result?
        return "ERROR";
      } else {
        ReadSome();

        // XXX Show progress...
        Print(stderr, APURPLE("."));
      }
    }
  }

 private:
  std::string hostname;
  std::string model_name;
  std::string api_key;
  std::unique_ptr<TLSClient> client;

  // State describing the HTTP response.
  enum class HTTPState { STATUS, HEADERS, BODY, BUSTED, COMPLETED, };
  HTTPState http_state = HTTPState::STATUS;
  // If true, then we have transfer-encoding: chunked.
  bool transfer_chunked = false;
  std::vector<std::pair<std::string, std::string>> headers;
  // The content of the HTTP response.
  ContiguousBuffer content;
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
