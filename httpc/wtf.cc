
#include <algorithm>
#include <cctype>
#include <cmath>
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
#include "base/stringprintf.h"
#include "color-util.h"
#include "markdown.h"
#include "model-client.h"
#include "net.h"
#include "process-util.h"
#include "rapidjson/document.h"
#include "timer.h"
#include "util.h"

static constexpr std::string PROMPT_DIR = "/c/code/sf_svn/httpc";
static std::string GetPrompt(std::string_view filename) {
  std::string path = Util::DirPlus(PROMPT_DIR, filename);
  std::string content = Util::ReadFile(path);
  CHECK(!content.empty()) << path;
  return content;
}

static void ConsumeWS(std::string_view *s) {
  while (!s->empty() && std::isspace((*s)[0])) {
    s->remove_prefix(1);
  }
}

static std::vector<std::string> SvnList(std::string_view dir) {
  // "svn list" uses the server's version of the files at
  // some revision, which might not be the newest one. This
  // is not what we want.
  //
  // "svn status -vq" will show everything we want, but we
  // have to remove some stuff to find the filename:
  // M             6997     6996 tom7         makefile
  std::string cmd = std::format("svn st -vq \"{}\"", dir);
  std::optional<std::string> out = ProcessUtil::GetOutput(cmd);
  CHECK(out.has_value()) << "Command failed: " << cmd;


  // Parse.
  std::vector<std::string> ret;
  for (std::string_view line : Util::SplitToLines(out.value())) {
    if (line.empty())
      continue;

    CHECK(line.size() > 8) << "Must have at least the 8 status chars?";
    line.remove_prefix(8);

    ConsumeWS(&line);
    // Now two columns of revision numbers or "-".
    while (!line.empty() && (line[0] == '-' || std::isdigit(line[0]))) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    while (!line.empty() && (line[0] == '-' || std::isdigit(line[0]))) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    // Now looking at username.
    while (!line.empty() && !std::isspace(line[0])) {
      line.remove_prefix(1);
    }
    ConsumeWS(&line);

    CHECK(!line.empty()) << "Line didn't contain filename?";

    // Don't include the directory itself.
    if (line != dir) {
      // svn st will already include the path. But use minimal
      // relative filenames when it's right here.
      if (dir == "." && Util::StartsWith(line, "./")) {
        line.remove_prefix(2);
      }
      ret.emplace_back(line);
    }
  }
  return ret;
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

static std::string IncludesPrompt(std::string_view question,
                                  std::string_view output,
                                  const std::map<std::string, int64_t> &files) {
  std::string filestring;
  for (const auto &[f, sz] : files) {
    AppendFormat(&filestring, "{: 8d}  {}\n", sz, f);
  }

  return std::format(
R"(Domain: AI programming assistance.

In this task, you'll see context (like computer code or an error
message) and a question from a user. Your task is not to answer the
question directly, but to guess what additional files would be
necessary to answer the question. The list of available files will be
given below, with their sizes. Your repsonse to the task will be in
JSON format, and will consist of your notes about the thought process,
and the list of files you would like to open.

The context for the user's question is this:
```
{}
```

The user's question is: "{}"

The available files are:
{}

Each file is listed with its byte size. The cost is directly proportional
to the file size. The priority is to answer the question, but as a
secondary concern, try to minimize the total size of files chosen.
Large files (more than 50kb) should be rarely chosen unless they are
clearly vital to the question. When looking at source code, header
files are often sufficient to understand the interface to a library.

Now it's time for your output. Given the user's question, what would
be the most important files to read in order to provide the background
information to answer it? Sometimes the question will be self-evident,
so your answer might be the empty list. You may only name files from
the list but can describe other missing information in an optional
separate field.

Your result is a JSON object that looks like this:

{{ "notes": "My own notes from considering the problem. Do I already know the answer? Why do I believe the contents of the files would be useful? How did I consider the file size?",
  "files": ["file1.h", "file2.h", ...],
  "missing": "Optional. Information I believe might be missing even if I read these files. Examples would include documentation, files that seem to exist exist but are not in the list of available files, or context on the problem that is unlikely to be in a file. This will be shown to the user."
}}

JSON:
)", output, question, filestring);
}

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

The context for the user's question is the output of some command:
```
{}
```

The user's question is: "{}"

<FILES>
{}
</FILES>

Now, please solve the user's question as best you can. Your output
is shown on the user's command-line terminal, so please be brief.
You may use markdown to set off code and command blocks, and to
bold phrases in the prose. Avoid bullet points, tables, and other
advanced markup. Unicode symbols are acceptable.

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

// Gemini likes to wrap json output in markdown.
static std::string StripMarkup(std::string_view json) {
  Util::RemoveOuterWhitespace(&json);
  if (json.starts_with("```json")) {
    json.remove_prefix(7);
  } else if (json.starts_with("```")) {
    json.remove_prefix(3);
  }

  if (json.ends_with("```")) {
    json.remove_suffix(3);
  }

  return std::string(json);
}

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  int verbose = 1;

  const std::string api_key =
    Util::NormalizeWhitespace(Util::ReadFile("d://tom//GEMINI_API_KEY"));
  CHECK(!api_key.empty());

  // pipe the output of a command (or paste on stdin)
  // and ask a question on the command line.

  // Files that are part of a public repository are offered
  // as potential inputs, if the model wants to see them.

  // Dirs to search for files.
  std::set<std::string> dirs = {"."};

  // Wildcards to never offer up.
  std::vector<std::string> exclude = {"*.png", "*.jpg"};

  std::optional<std::string> question;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-v") {
      verbose++;
    } else if (arg == "-dir") {
      CHECK(i + 1 < argc);
      i++;
      dirs.insert(argv[i]);
    } else {
      CHECK(!question.has_value()) << "Quote the question on the "
        "command line.";
      question = {std::string(arg)};
    }
  }

  if (!question.has_value()) {
    question = {"What's going on here? Can you fix it?"};
  }

  std::string output = Util::ReadStdin();

  // TODO: Read .clangd in the current directory for dirs to mine.

  // Relative paths to files, with sizes.
  std::map<std::string, int64_t> files;
  for (const std::string &dir : dirs) {
    for (const std::string &file : SvnList(dir)) {
      if (!files.contains(file) &&
          !Excluded(exclude, file)) {

        size_t size = FileSize(file);
        if (size > 0) {
          files[file] = size;
        }
      }
    }
  }

  // Construct prompt to guess at files to include (cheap model).

  Timer include_timer;
  std::vector<std::string> to_include = [&]{
      CHECK(question.has_value());
      std::string includes_prompt =
        IncludesPrompt(question.value(), output, files);

      std::unique_ptr<ModelClient> cheap =
        ModelClient::Create(Model::GEMINI_CHEAPEST, api_key);

      CHECK(cheap.get() != nullptr);
      cheap->SetVerbose(verbose);

      std::string json = StripMarkup(cheap->Infer(includes_prompt));

      std::vector<std::string> to_include;

      {
        using namespace rapidjson;
        Document document;
        CHECK(!document.Parse(json.c_str()).HasParseError());

        CHECK(document.IsObject());
        CHECK(document.HasMember("notes"));
        if (document.HasMember("files") && document["files"].IsArray()) {
          for (const Value &v : document["files"].GetArray()) {
            CHECK(v.IsString());
            std::string file = v.GetString();
            if (Util::StartsWith(file, "./")) {
              file = file.substr(2);
            }
            if (files.contains(file)) {
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
  }

  if (verbose > 0) {
    Print(AWHITE("To include") ":\n");
    for (const std::string &f : to_include) {
      Print("  {}\n", f);
    }
  }

  // Read the file content.
  std::string file_text;
  for (const std::string &f : to_include) {
    AppendFormat(&file_text,
                 "\n"
                 "The file {}:\n"
                 "```\n"
                 "{}"
                 "```\n", f, Util::ReadFile(f));
  }


  Timer solve_timer;
  CHECK(question.has_value());
  std::string solve_prompt =
    SolvePrompt(question.value(), output, file_text);

  std::unique_ptr<ModelClient> best =
    ModelClient::Create(Model::GEMINI_BEST, api_key);

  CHECK(best.get() != nullptr);
  best->SetVerbose(verbose);

  std::string json = StripMarkup(best->Infer(solve_prompt));

  Print("Solve phase done in {}\n", ANSI::Time(solve_timer.Seconds()));
  Print("\n\n" AWHITE("Raw response") ":\n"
        AGREY("{}"), json);

  {
    using namespace rapidjson;
    Document document;
    CHECK(!document.Parse(json.c_str()).HasParseError());

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
