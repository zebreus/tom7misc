
#include <cctype>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "markdown.h"
#include "model-client.h"
#include "model-util.h"
#include "net.h"
#include "rapidjson/document.h"
#include "timer.h"
#include "util.h"

static bool Ask(int verbose,
                Model model,
                std::string_view api_key,
                std::string_view question) {
  std::unique_ptr<ModelClient> client =
      ModelClient::Create(model, api_key);
  if (client == nullptr) {
    return false;
  }
  client->SetVerbose(verbose);

  std::string prompt = std::format(
      R"(Domain: Command-line AI assistant tool.

The user has a question:
```
{}
```

Please provide a helpful answer or state clearly why you cannot.
Answer in prose, but please be brief and precise. The result will
appear on the user's terminal, so don't output more than a page unless
the question requires it. You can use light markdown: bold text,
inline code, code snippets, and bullet points for lists. Headings, and
links also work but should be rare in this context. Unicode letters
and symbols work correctly.

If the question is about code or can be solved with a computer program,
it is good to include code that directly solves the user's problem.
Infer style and language from the input files and attempt to match it.
If there is no code present but code is still useful in the output,
C++ and JavaScript are good options. Avoid third-party libraries.
When you propose modifications to input files, don't repeat large sections
of them. The user has these files open in their editor and prefers to
apply edits manually. Just give enough context so that it will be
clear to the user where proposed edits are supposed to apply.
Keep any code under 78 columns. Follow the commenting style of
the surrounding code (typically you should not write comments that
simply say what the code that following does, or number the steps,
but it can be helpful to leave brief notes about subtle things).

You only get one shot at this; asking the user direct questions on
what to do next is not appropriate. If you determine that there is not
enough information to answer the user's question, you may explain the
hypotheses and suggest an action that would be diagnostic (so that the
user can solve it on their own, or prepare a better question). You
will also express your confidence that the solution is correct on a
scale from 0 to 100%.

Your result is a JSON object that looks like this:

{{ "notes": "My own notes from considering the problem. These notes are not shown to the user. ",
   "response": "Your answer to show to the user. Brief prose; light markdown is allowed.",
   "confidence": 85,
}}

JSON:
)",
      question);

  std::string raw = client->Infer(prompt);
  std::optional<std::string> oj = ModelUtil::FindOneJSONObject(raw);
  if (!oj.has_value()) {
    Print(ARED("Unable to find a JSON object!") "\n"
          "\n"
          AGREY("{}\n"), raw);
    return false;
  }
  const std::string &json = oj.value();

  rapidjson::Document document;
  if (document.Parse(json).HasParseError() || !document.IsObject()) {
    return false;
  }

  if (document.HasMember("response") && document["response"].IsString()) {
    std::string response = document["response"].GetString();
    Markdown::Document doc = Markdown::Parse(response);
    Print("\n{}\n", Markdown::ToColorTerminal(doc));
    return true;
  }

  return false;
}


#define PROMPT_COLOR ANSI_FG(138, 188, 242)
#define RESP_COLOR ANSI_FG(207, 138, 242)

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  constexpr std::string_view usage = "Quote the question on the "
    "command line, or use - for stdin.";

  int verbose = 1;

  std::optional<std::string> question;

  // Not used yet.
  std::set<std::string> dirs;

  Model model = Model::GEMINI_MEDIUM;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg.starts_with("-")) {
      if (arg == "-v") {
        verbose++;
        continue;
      }

      if (arg == "-dir") {
        CHECK(i + 1 < argc);
        i++;
        dirs.insert(argv[i]);
        continue;
      }

      if (std::optional<Model> m =
          ModelClient::ModelByName(arg.substr(1))) {
        model = m.value();
        continue;
      }
    }

    CHECK(!question.has_value()) << usage;
    question = {std::string(arg)};
  }

  if (!question.has_value()) {
    LOG(FATAL) << usage;
  }

  if (question == "-") {
    question = {Util::ReadStdin()};
  }

  const std::string api_key = ModelUtil::GetAPIKey();

  return Ask(verbose, model, api_key, question.value()) ? 0 : 1;
}
