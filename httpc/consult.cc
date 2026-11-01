
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "model-client.h"
#include "model-util.h"
#include "net.h"
#include "util.h"

static bool Consult(int verbose,
                    bool log,
                    Model model,
                    std::string_view api_key,
                    std::string_view question) {
  std::unique_ptr<ModelClient> client =
      ModelClient::Create(model, api_key);
  if (client == nullptr) {
    return false;
  }
  client->SetVerbose(verbose);
  client->SetLog(log);

  std::string prompt = std::format(
      R"(Domain: Mathematics.

We are working on an open mathematical problem. Your goal is to provide
mathematical insight, to discover reasoning mistakes and incorrect
assumptions, to make connections with theorems and branches of mathematics,
and provide a brief of the best 4-8 avenues of exploration.

Next, you'll see the problem described inside the <PROBLEM></PROBLEM>
tags:

<PROBLEM>
{}
</PROBLEM>

You only get one shot at this; asking the user direct questions on
what to do next is not appropriate. You may include notes about things
that appear incorrect or incomplete in the problem description.

Please provide the best 4-8 ideas for how to make progress on this
problem. The best idea is a direct solution that is self-evident, or
a breakthrough insight that makes everything fall into place, but
this may be unrealistic for difficult problems. Some qualities that
can make for a good idea:
 * It is genuinely new, not repeating things we already know.
 * It makes an unexpected connection with pre-existing theorems or
   mathematical techniques, especially from areas of mathematics
   that are not already considered.
 * For a hypothesis, it comes with an efficient way to falsify it
   (e.g. with a computational test).
 * It reframes the question, assumptions, or target in different
   mathematical language that sidesteps difficulties.
 * It breaks the problem down into a series of highly plausible
   lemmata.
 * It is structural, analytic, and general. It does not rely on
   brute-force techniques, deep case analysis, or cover (unless the
   number of cases is small).

Include at least one idea that is eyebrow-raising, off-the-wall, and
provocative, but *just might work*. You may also include brief notes
about ideas you considered but ruled out, or just aren't sure about,
but mark these as separate from the main ideas list.

This is for beloved nerd Tom 7, so be very inventive and clever.
However, the ideas are only useful if they are mathematically real,
and can ultimately be made formal.

Your result should be in markdown using LaTeX or unicode for math,
with no more than 4000 words.

Your markdown response:
)",
      question);

  std::string raw = client->Infer(prompt);

  Print("\n{}\n", raw);
  return true;
}


#define PROMPT_COLOR ANSI_FG(138, 188, 242)
#define RESP_COLOR ANSI_FG(207, 138, 242)

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  constexpr std::string_view usage = "Quote the question on the "
    "command line, or use - for stdin.";

  int verbose = 0;
  bool log = false;

  std::optional<std::string> filename;

  Model model = Model::GEMINI_BEST;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg.starts_with("-")) {
      if (arg == "-v") {
        verbose++;
        continue;
      }

      if (arg == "-log") {
        log = true;
        continue;
      }

      if (std::optional<Model> argmodel =
          ModelClient::IsModelFlag(arg)) {
        model = argmodel.value();
        Print(stderr, "Using " APURPLE("{}") ".\n",
              ModelClient::ModelName(model));
        continue;
      }

    }

    CHECK(!filename.has_value()) << usage;
    filename = {std::string(arg)};
  }

  CHECK(filename.has_value()) << usage;

  std::string problem;
  if (filename.value() == "-") {
    problem = {Util::ReadStdin()};
  } else {
    problem = Util::ReadFile(filename.value());
  }

  problem = Util::LoseOuterWhitespace(problem);

  CHECK(!problem.empty()) << usage << "\n\nNeed a non-empty problem.";

  const std::string api_key = ModelUtil::GetAPIKey();

  return Consult(verbose, log, model, api_key, problem) ? 0 : 1;
}
