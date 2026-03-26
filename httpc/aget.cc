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

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  int verbose = 1;
  Model model = Model::GEMINI_CHEAPEST;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-cheap") {
      model = Model::GEMINI_CHEAPEST;
    } else if (arg == "-fast") {
      model = Model::GEMINI_FASTEST;
    } else if (arg == "-medium") {
      model = Model::GEMINI_MEDIUM;
    } else if (arg == "-best") {
      model = Model::GEMINI_BEST;
    } else if (arg == "-v") {
      verbose++;
    } else {
      LOG(FATAL) << "Command line argument not understood: " << arg;
    }
  }

  const std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());

  std::unique_ptr<ModelClient> client =
    ModelClient::Create(model, api_key);
  CHECK(client.get() != nullptr);
  client->SetVerbose(verbose);

  // Read stdin.
  std::string prompt = Util::ReadStdin();

  Timer response_timer;
  std::string result = client->Infer(prompt);
  double response_time = response_timer.Seconds();

  Print("{}\n", result);

  Print(stderr, "Response took {}\n", ANSI::Time(response_time));

  Net::Shutdown();
  return 0;
}
