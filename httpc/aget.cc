#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "model-client.h"
#include "net.h"
#include "timer.h"
#include "util.h"
#include "status-bar.h"

int main(int argc, char* argv[]) {
  ANSI::Init();
  Net::Init();

  if (false) {
    StatusBar status(3);
    for (int i = 0; i < 10; i++) {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(500ms);
      status.EmitStatus({"first line", std::format(ABLUE("{}"), i),
          APURPLE("bottom line")});
    }
    exit(-1);
  }

  const std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());

  std::unique_ptr<ModelClient> client =
    ModelClient::Create(Model::GEMINI_FASTEST, api_key);
  CHECK(client.get() != nullptr);
  client->SetVerbose(1);

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
  std::string result = client->Infer(prompt);
  double response_time = response_timer.Seconds();

  Print("{}\n", result);

  Print(stderr, "Response took {}\n", ANSI::Time(response_time));

  Net::Shutdown();
  return 0;
}
