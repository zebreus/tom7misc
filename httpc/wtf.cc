
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "markdown.h"
#include "model-client.h"
#include "model-tasks.h"
#include "model-util.h"
#include "net.h"
#include "rapidjson/document.h"
#include "timer.h"
#include "util.h"

static std::string SolvePrompt(std::string_view question,
                               std::string_view output,
                               std::string_view filetext) {
  return std::format(
R"(Domain: AI programming assistance.

In this task, you'll see some context (like computer code or the
output of a command) and a question from the user. You will also
see the contents of some files (like source code) that might help
answer the user's question. There may be irrelevant information
present; focus on the user's question.

The context for the user's question is this:
```
{}
```

The user's question is: "{}"

<FILES>
{}
</FILES>

Now, please solve the user's question as best you can. Your output is
shown on the user's command-line terminal, so please be brief. You may
use markdown (inside the JSON string literals) to set off code and
command blocks, and to bold phrases in the prose. Avoid bullet points,
tables, and other advanced markup. Unicode symbols are acceptable.

It is good to include code that directly solves the user's problem,
such as a correction to a typo found in the input files. Infer
style from the input files and attempt to match it. If you
include code, don't repeat large sections from the input files.
The user has these files open in their editor and prefers to
apply edits manually. Just give enough context so that it will be
clear to the user where proposed edits are supposed to apply.
Keep any code under 78 columns. Follow the commenting style of
the surrounding code (typically you should not write comments that
simply say what the code that following does, or number the steps,
but it can be helpful to leave brief notes about subtle things).

You only get one shot at this; asking the user direct questions on
what to do next is not appropriate. If you determine that there is
not enough information to solve it, you may explain the hypotheses
and suggest an action that would be diagnostic. You will also express
your confidence that the solution is correct on a scale from 0 to 100%.

Your result is a JSON object that looks like this:

{{ "notes": "My own notes from considering the problem.",
   "missing": "Optional. Important information I didn't have access to in the files or in my knowledge; I had to guess at it or its absence caused me to fail. This will be shown to the user.",
   "solution": "My solution to the problem. Brief prose; light markdown is acceptable.",
   "confidence": 85
}}

JSON:
)", output, question, filetext);
}


#define PROMPT_COLOR ANSI_FG(138, 188, 242)
#define RESP_COLOR ANSI_FG(207, 138, 242)

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  int verbose = 1;

  const std::string api_key = ModelUtil::GetAPIKey();

  // pipe the output of a command (or paste on stdin)
  // and ask a question on the command line.

  // Files that are part of a public repository are offered
  // as potential inputs, if the model wants to see them.


  // Dirs to search for files.
  std::set<std::string> dirs = {"."};

  std::optional<std::string> question;

  ModelUtil::FileCollection files;
  // Wildcards to never offer up.
  files.AddExcludePattern("*.png");
  files.AddExcludePattern("*.jpg");
  files.AddExcludePattern("*COPYING");
  files.AddExcludePattern("*LICENSE");
  files.AddExcludePattern("*APACHE20.txt");
  files.AddExcludePattern("*CONTRIBUTORS");

  // The current file we're looking at.
  std::string file_arg;
  bool fast = false;
  // In emacs mode, ANSI colors work but not stuff like status bar.
  bool emacs = false;

  std::optional<Model> model_from_flags;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-v") {
      verbose++;

    } else if (arg == "-fast") {
      fast = true;

    } else if (arg == "-emacs") {
      emacs = true;

    } else if (arg == "-dir") {
      CHECK(i + 1 < argc);
      i++;
      Print("Considering " AYELLOW("{}") " (command-line)\n", argv[i]);
      dirs.insert(argv[i]);

    } else if (arg == "-file") {
      CHECK(file_arg.empty()) << "At most one -file.";
      CHECK(i + 1 < argc);
      i++;
      file_arg = argv[i];

    } else if (arg == "-config") {
      CHECK(i + 1 < argc);
      i++;
      Print("Read config " ABLUE("{}") "\n", argv[i]);
      files.AddConfig(argv[i]);

    } else if (std::optional<Model> argmodel =
               ModelClient::IsModelFlag(arg)) {
      model_from_flags = {argmodel};

    } else {
      CHECK(!question.has_value()) << "Quote the question on the "
        "command line. (Already saw " << question.value() << ")";
      question = {std::string(arg)};
    }
  }

  Model solve_model = Model::GEMINI_BEST;
  if (model_from_flags.has_value()) {
    solve_model = model_from_flags.value();
  } else if (fast) {
    solve_model = Model::GEMINI_MEDIUM;
  }

  Print("Using " APURPLE("{}") " for solve phase.\n",
        ModelClient::ModelName(solve_model));


  if (!question.has_value()) {
    question = {"What's going on here? Can you fix it?"};
  }

  const std::string file = [&]() -> std::string {
      if (file_arg.empty())
        return "";

      std::string current_wd = Util::PathOf(file_arg);
      std::string current_file = Util::FileOf(file_arg);
      CHECK(Util::ChangeDir(current_wd)) << "Couldn't change directory to the "
        "location of " << file_arg << " which is " << current_wd << " ..?";
      return current_file;
    }();

  std::string output = Util::ReadStdin();

  if (!fast) {
    // Probably should use paths here too.
    for (const std::string &d : ModelUtil::IncludeDirs(file)) {
      // Print("Via clangd: " AYELLOW("{}") "\n", d);
      dirs.insert(d);
    }

    for (const std::string &dir : dirs) {
      files.AddSvnFiles(dir);
    }

    // Use .model-config in the same directory as the target file
    // to find explicitly allowlisted files (e.g. project.txt).
    if (Util::ExistsFile(".model-config")) {
      files.AddConfig(".model-config");
    }
  }

  // The current file is always available, even if not checked in.
  // We don't necessarily read it, though (the provided context
  // might be enough).
  if (!file.empty()) files.AddFile(file);
  files.DescribeFile(file,
                     "The file the user is currently looking at.");

  ModelUtil::AvailableFiles available = files.GetAvailable();

  Print("List of available files:\n");
  for (const auto &[f, af] : available.files) {
    Print("  " AWHITE("{}") " = {} " AGREY("({})") "\n",
          f, af.path.string(), af.bytes);
  }

  // TODO: If nothing was highlighted, include the file. But with
  // a region highlighted, we could just skip the file?

  // Construct prompt to guess at files to include (cheap model).
  Timer include_timer;
  std::vector<std::string> to_include = [&] -> std::vector<std::string> {
      if (fast) return {file};
      CHECK(question.has_value());

      std::unique_ptr<ModelClient> client =
          ModelClient::Create(Model::GEMINI_MEDIUM, api_key);
      client->SetVerbose(verbose);

      ModelTasks::ChooseFilesOptions opt;
      opt.current_filename = file;
      opt.can_fail = true;
      opt.can_answer = true;

      ModelTasks::ChooseFilesResult result =
          ModelTasks::ChooseFiles(client.get(), question.value(),
                                  available, opt);

      std::vector<std::string> to_include;
      if (const ModelTasks::ChosenFiles *cf =
              std::get_if<ModelTasks::ChosenFiles>(&result)) {
        to_include = cf->files;
        if (!cf->message.empty()) {
          Markdown::Document doc = Markdown::Parse(cf->message);
          Print("\n{}\n", Markdown::ToColorTerminal(doc));
        }
      } else if (const ModelTasks::Answer *ans =
                     std::get_if<ModelTasks::Answer>(&result)) {
        if (!ans->message.empty()) {
          Markdown::Document doc = Markdown::Parse(ans->message);
          Print("\n{}\n", Markdown::ToColorTerminal(doc));
        }
        exit(0);
      } else if (const ModelTasks::Failure *fail =
                     std::get_if<ModelTasks::Failure>(&result)) {
        if (!fail->message.empty()) {
          Markdown::Document doc = Markdown::Parse(fail->message);
          Print("\n{}\n", Markdown::ToColorTerminal(doc));
        }
        exit(1);
      }

      return to_include;
    }();

  if (!fast) {
    if (verbose > 0) {
      Print("Include phase done in {}\n", ANSI::Time(include_timer.Seconds()));
    }

    if (verbose > 0) {
      Print(AWHITE("To include") ":\n");
      for (const std::string &f : to_include) {
        Print("  {}\n", f);
      }
    }
  }

  // Read the file content.
  std::string file_text =
    ModelUtil::TextualizeChosenFiles(available, to_include);

  Timer solve_timer;
  CHECK(question.has_value());
  std::string solve_prompt =
    SolvePrompt(question.value(), output, file_text);

  std::unique_ptr<ModelClient> best =
    ModelClient::Create(solve_model, api_key);

  CHECK(best.get() != nullptr);
  best->SetVerbose(verbose);

  if (emacs) Print("<STATUS>\n");
  std::string raw = best->Infer(solve_prompt);
  if (emacs) Print("</STATUS>\n");
  Print("Solve phase done in {}\n", ANSI::Time(solve_timer.Seconds()));
  std::string json = ModelUtil::FindOneJSONObject(raw).value_or("");
  if (json.empty()) {
    Print(ARED("Unable to find a JSON object!") "\n"
          "\n"
          AGREY("{}\n"), raw);
  } else {
    Print("\n\n" AWHITE("Raw json") ":\n"
          AGREY("{}"), json);
  }
  fflush(stdout);

  {
    using namespace rapidjson;
    Document document = ModelUtil::ParseSloppyOrDie(json);

    CHECK(document.IsObject());
    CHECK(document.HasMember("notes"));

    int confidence = 0;
    if (document.HasMember("confidence") &&
        document["confidence"].IsNumber()) {
      confidence = std::clamp(
          (int)std::round(document["confidence"].GetDouble()),
          0, 100);
    }

    std::string solution;
    if (document.HasMember("solution") &&
        document["solution"].IsString()) {
      solution = document["solution"].GetString();
    } else {
      // Could output from notes, missing, etc.?
      solution = "UNSOLVED";
    }

    int w = std::max(16, ANSI::TerminalWidth().value_or(80) - 8);
    uint32_t pcolor = ColorUtil::LinearGradient32(
        ColorUtil::PROBABILITY_TEXT, confidence/100.0);
    std::string title =
      std::format(" " ANSI_FG(255, 255, 255) "Solution"
                  ANSI_FG(200, 200, 200) " (Confidence "
                  "{}{}%"
                  ANSI_FG(200, 200, 200) ") ",
                  ANSI::ForegroundRGB32(pcolor),
                  confidence);
    int slack = std::max(0, w - ANSI::StringWidth(title));
    title.append(slack, ' ');

    Print("\n"
          ANSI_BG(0, 4, 89) " " ANSI_FG(245, 237, 154) "☻"
          "{}" ANSI_RESET "\n\n", title);

    Markdown::Document doc = Markdown::Parse(solution);
    Print("\n{}\n", Markdown::ToColorTerminal(doc));
  }


  return 0;
}
