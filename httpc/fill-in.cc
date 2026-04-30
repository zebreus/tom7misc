
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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

#define PROMPT_COLOR ANSI_FG(138, 188, 242)
#define RESP_COLOR ANSI_FG(207, 138, 242)

static std::string IncludesPrompt(
    std::string_view current_file,
    std::string_view current_file_contents,
    std::string_view request,
    const ModelUtil::AvailableFiles &available) {
  std::string filestring = available.Textualize();

  return std::format(
R"(Domain: AI programming assistance.

In this task, you'll see a file that the user is currently looking
at, with a part marked `** FILL IN ANSWER HERE **`. You'll also see
a request or question from the user. Your task is not to answer the
question directly, but to guess what additional files would be
necessary to do a good job completing the task. For example, in the
common case that the user's request is to write some code, you
should try to determine what files would be needed as context
in order to write that code correctly. For example, you might want
to load the header file for a non-standard library that is related
to the task. If the user's request is to write tests for some code,
then we likely need to see both the header and implementation for
that code in order to know how to test it well. Files that describe
the project or contain a style guide for the current language are
useful too. The "llm" directory contains style guides that apply
to all projects.

You may only choose from the list of available files. The available
files will be given below, with their sizes. Your repsonse to the task
will be in JSON format, and will consist of your notes about the
thought process, and the list of files you would like to open.

The file that the user is looking at is called `{}`, and it contains:
```
{}
```

The user's request or question is:
```
{}
```

Remember: You're not trying to perform the request yet; you should just
select files that we might need to see to solve it.

The available files are:
{}

Each file is listed with its byte size. The cost is directly
proportional to the file size. The priority is to answer the question,
but as a secondary concern, try to minimize the total size of files
chosen. Large files (more than 50kb) should be rarely chosen unless
they are clearly vital to the question. When looking at source code,
header files are often sufficient to understand the interface to a
library.

Now it's time for your output. Given the user's request, what would be
the most important files to read in order to provide the background
information to accomplish it? Sometimes the question will be
self-evident, or only require the context of the current file, so your
answer might be the empty list. You may only name files from the list
but can describe other missing information in an optional separate
field.

Your result is a JSON object that looks like this:

{{ "notes": "My own notes from considering the problem. Do I already know how to do it without additional context? Why do I believe the contents of the files would be useful? How did I consider the file size?",
  "files": ["file1.h", "file2.h", ...],
  "missing": "Optional. Information I believe might be missing even if I read these files. Examples would include documentation, files that seem to exist exist but are not in the list of available files, or context on the problem that is unlikely to be in a file. This will be shown to the user."
}}

JSON:
)",
current_file, current_file_contents,
request, filestring);
}

std::string GenerateFill(
    std::string_view current_file,
    std::string_view current_file_contents,
    std::string_view request,
    // remainder of files
    std::string_view filetext) {

  return std::format(
R"(Domain: AI programming assistance.

In this task, you'll see a file that the user is currently looking at,
with a part marked `** FILL IN ANSWER HERE **`. You'll also see a
request or question from the user about how to fill in the answer.
Additionally, there will be the contents of some files (like source
code headers) in a section delimited <FILES></FILES> that might help.
There may be irrelevant information present; focus on the user's question.


The file that the user is looking at is called `{}`, and it contains:
```
{}
```

The user's request or question is:
```
{}
```

<FILES>
{}
</FILES>

Now, please fulfill the user's request as best you can. Your primary
output will be text to fill in the part marked
`** FILL IN ANSWER HERE **`, but you will also be asked to produce
your notes about your approach to the problem, and your confidence
in the solution. You may also write a message to the user that explains
what you did, or asks a follow-up question, or tells them about other
changes they need to make elsewhere.

If the request is asking you to write code, infer style from the input
files (especially the current one) and attempt to match it. Ideally,
the code will work as written to address the user's request when it
literally replaces the `** FILL IN ANSWER HERE **` marker. However,
the user is interacting with the code and will read whatever you put
there. Keep any code under 78 columns. Follow the commenting style of
the surrounding code (typically you should not write comments that
simply say what the code that following does, or number the steps, but
it can be helpful to leave brief notes about subtle things).

If the user's request includes a code snippet, this code snippet
probably used to be where the marker now is. The ideal answer would
keep the code snippet as intact as possible, making only the
modifications needed to . Avoid gratuitous rewrites, like changing
the names of variables or changing style to "best practices," unless
this is part of the request from the user or necessary to fulfill
the request. If you see an objective mistake (bug or typo), you
should fix it and note this in your message for the user.

The message to the user is optional. It will show on their terminal,
so it should be fairly brief. You may use markdown to set off code and
command blocks, to make bullet-point lists, and to bold phrases in the
prose. Avoid tables and other advanced markup. Unicode symbols are
acceptable.


You only get one shot at this, so be methodical and precise. If you
determine that there is not enough information to fulfill the user's
request, you may explain the hypotheses and propose modifications that
would be diagnostic (e.g., in code, the insertion of new debugging
output). You will also express your confidence that the solution is
correct on a scale from 0 to 100%.

Your result is a JSON object that looks like this:

{{ "notes": "My own notes from considering the problem.",
   "replacement": "My replacement for the marker, as literal text. This is often multiple lines, escaping characters like newlines that have special meaning in JSON.",
   "message": "Optional message to the user. Brief prose, which can use light markdown.",
   "confidence": 85
}}

JSON:
)", current_file, current_file_contents, request, filetext);
}

static bool Excluded(const std::vector<std::string> &exclude,
                     std::string_view file) {
  for (const std::string &wc : exclude) {
    if (Util::MatchesWildcard(wc, file)) {
      return true;
    }
  }

  return false;
}

static size_t FileSize(std::string_view path) {
  // PERF use stat! I must have this somewhere?
  return Util::ReadFile(path).size();
}

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  int verbose = 1;

  const std::string api_key = ModelUtil::GetAPIKey();

  Model solve_model = Model::GEMINI_BEST;

  // Dirs to search for files.
  std::set<std::string> dirs = {"."};

  std::string file_arg;

  ModelUtil::FileCollection files;

  // Wildcards to never offer up.
  files.AddExcludePattern("*.png");
  files.AddExcludePattern("*.jpg");
  files.AddExcludePattern("*.ccz");
  files.AddExcludePattern("*COPYING");
  files.AddExcludePattern("*LICENSE");
  files.AddExcludePattern("*APACHE20.txt");
  files.AddExcludePattern("*CONTRIBUTORS");

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-v") {
      verbose++;

    } else if (arg == "-config") {
      CHECK(i + 1 < argc);
      i++;
      Print("Read config " ABLUE("{}") "\n", argv[i]);
      files.AddConfig(argv[i]);

    } else if (arg == "-dir") {
      CHECK(i + 1 < argc);
      i++;
      Print("Considering " AYELLOW("{}") " (command-line)\n", argv[i]);
      dirs.insert(argv[i]);

    } else if (std::optional<Model> argmodel =
               ModelClient::IsModelFlag(arg)) {
      solve_model = argmodel.value();
      Print("Using " APURPLE("{}") " for solve phase.\n",
            ModelClient::ModelName(solve_model));

    } else {
      CHECK(file_arg.empty()) << "Just one file on the command line.";
      file_arg = std::string(arg);
    }
  }

  CHECK(!file_arg.empty()) << "Need a file on the command line!";

  // Use the location of the file on the command line as
  // the current one.
  std::string current_wd = Util::PathOf(file_arg);
  std::string current_file = Util::FileOf(file_arg);
  CHECK(Util::ChangeDir(current_wd)) << "Couldn't change directory to the "
    "location of " << file_arg << " which is " << current_wd << " ..?";

  std::string current_file_contents = Util::ReadFile(file_arg);
  CHECK(!current_file_contents.empty()) << file_arg;

  std::string request = Util::ReadStdin();
  if (request.empty()) {
    request = "Can you fill this part in?";
  }

  if (Util::ExistsFile(".model-config")) {
    files.AddConfig(".model-config");
  }

  // Probably should use paths here too.
  for (const std::string &d : ModelUtil::IncludeDirs(current_file)) {
    // Print("Via clangd: " AYELLOW("{}") "\n", d);
    dirs.insert(d);
  }

  for (const std::string &dir : dirs) {
    files.AddSvnFiles(dir);
  }

  ModelUtil::AvailableFiles available = files.GetAvailable();

  if (verbose) {
    Print("List of available files:\n");
    for (const auto &[f, af] : available.files) {
      Print("  " AWHITE("{}") " = {} " AGREY("({})") "\n",
            f, af.path.string(), af.bytes);
    }
  }

  Print("{}\n", available.Textualize());
  fflush(stdout);

  // Construct prompt to guess at files to include (cheap model).

  Timer include_timer;
  std::vector<std::string> to_include = [&]() -> std::vector<std::string> {
      if (available.files.empty()) {
        Print("No files available! Skipping that phase.\n");
        return {};
      }

      CHECK(!request.empty());
      std::string includes_prompt =
        IncludesPrompt(current_file, current_file_contents,
                       request, available);

      std::unique_ptr<ModelClient> cheap =
        ModelClient::Create(Model::GEMINI_CHEAPEST, api_key);

      CHECK(cheap.get() != nullptr);
      cheap->SetVerbose(verbose);

      std::string raw = cheap->Infer(includes_prompt);
      std::string json = ModelUtil::FindOneJSONObject(raw).value_or("");
      if (json.empty()) {
        Print(ARED("Unable to find a JSON object!") "\n"
              "\n"
              AGREY("{}\n"), raw);
        return {};
      }

      std::vector<std::string> to_include;

      {
        using namespace rapidjson;
        Document document = ModelUtil::ParseSloppyOrDie(json);

        CHECK(document.IsObject());
        CHECK(document.HasMember("notes"));
        if (document.HasMember("files") && document["files"].IsArray()) {
          for (const Value &v : document["files"].GetArray()) {
            CHECK(v.IsString());
            std::string file = v.GetString();
            if (Util::StartsWith(file, "./")) {
              file = file.substr(2);
            }
            if (available.files.contains(file)) {
              to_include.push_back(file);
            } else {
              Print(AORANGE("Warning") ": Unavailable file chosen. {}\n",
                    file);
            }
          }
        }

        if (document.HasMember("missing")) {
          CHECK(document["missing"].IsString());
          std::string missing = document["missing"].GetString();
          if (!missing.empty()) {
            Print(AWHITE("Model thinks this is missing") ":\n"
                  RESP_COLOR "{}" ANSI_RESET "\n",
                  missing);
            // Maybe prompt to continue in this case?
          }
        }
      }

      return to_include;
    }();
  if (verbose > 0) {
    Print("Include phase done in {}\n", ANSI::Time(include_timer.Seconds()));
    fflush(stdout);
  }

  if (verbose > 0) {
    Print(AWHITE("To include") ":\n");
    for (const std::string &f : to_include) {
      Print("  {}\n", f);
    }
    fflush(stdout);
  }

  // Read the file content.
  std::string file_text = ModelUtil::TextualizeChosenFiles(available,
                                                           to_include);

  Timer solve_timer;
  CHECK(!request.empty());
  std::string solve_prompt =
    GenerateFill(current_file, current_file_contents, request, file_text);

  std::unique_ptr<ModelClient> best =
    ModelClient::Create(solve_model, api_key);

  CHECK(best.get() != nullptr);
  best->SetVerbose(verbose);

  std::string raw = best->Infer(solve_prompt);
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

  bool failed = false;
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

    std::string replacement;
    if (document.HasMember("replacement") &&
        document["replacement"].IsString()) {
      replacement = document["replacement"].GetString();
    } else {
      failed = true;
      replacement = request;
    }

    std::string message;
    if (document.HasMember("message") &&
        document["message"].IsString()) {
      message = document["message"].GetString();
    }

    if (!message.empty()) {
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

      Markdown::Document doc = Markdown::Parse(message);
      Print("\n{}\n", Markdown::ToColorTerminal(doc));
    }

    Print("<REPLACEMENT>{}</REPLACEMENT>", replacement);
    fflush(stdout);
  }

  if (failed) {
    Print(ARED("sorry :(") "\n");
  }

  return 0;
}
