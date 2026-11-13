
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "model-client.h"
#include "model-util.h"
#include "net.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"
#include "vector-util.h"

static std::string MakeBackground(
    const std::vector<std::pair<std::string, std::string>> &bgdocs) {
  if (bgdocs.empty()) return "";

  std::string bg = "The following documents are provided for background only;\n"
    "they may contain useful facts for solving the problem, but the problem\n"
    "posed below is the focus. Some of this information may be irrelevant.\n"
    "Each document appears in a <BACKGROUND></BACKGROUND> tag with its\n"
    "filename.\n\n";

  for (const auto &[f, c] : bgdocs) {
    AppendFormat(&bg, "<BACKGROUND file=\"{}\">\n"
                 "{}\n"
                 "</BACKGROUND>\n\n", f, c);
  }

  return bg;
}

static std::string ConsultAs(
    int verbose,
    bool log,
    Model model,
    std::string_view api_key,
    const std::vector<std::pair<std::string, std::string>> &bgdocs,
    std::string_view question,
    std::string_view persona) {
  std::unique_ptr<ModelClient> client =
      ModelClient::Create(model, api_key);
  if (client == nullptr) {
    return "";
  }
  client->SetVerbose(verbose);
  client->SetLog(log);

  std::string bg = MakeBackground(bgdocs);

  std::string prompt = std::format(
      R"(Domain: Mathematics.

We are working on an open mathematical problem. Your goal is to provide
mathematical insight from a specific mathematical perspective: How
would {} approach this problem? You'll provide two or three ideas that
might solve or make concrete progress on it.

{}
Next, you'll see the specific problem described inside the
<PROBLEM></PROBLEM> tags:

<PROBLEM>
{}
</PROBLEM>

You only get one shot at this; asking the user direct questions on
what to do next is not appropriate. You may include notes about things
that appear incorrect or incomplete in the problem description.

Ask yourself: What would {} do? What ideas from their unique
mathematical toolbox would be most applicable? How would they
formulate an attack? How could they reduce part of the problem to a
known mathematical object or theorem? What stroke of insight from
their particular expertise would cause the pieces to fall into place?
Think carefully about the problem, and check your claims before
relying on them.

Provide the best overall idea, and a second idea if you have more than
one promising one in mind. Also include an idea that is
eyebrow-raising, off-the-wall, and provocative, but *just might work*.
Qualities of a good idea:
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

This is for beloved nerd Tom 7, so be very inventive and clever.
However, the ideas are only useful if they are mathematically real,
and can ultimately be made formal.

Just output one paragraph per idea, using markdown with LaTeX or
unicode for math, with no more than 400 words each. Check your
work.

Your markdown response:
)",
      persona, bg, question, persona);

  return Util::LoseOuterWhitespace(client->Infer(prompt));
}


static bool Consult(
    int verbose,
    bool log,
    Model model,
    std::string_view api_key,
    const std::vector<std::pair<std::string, std::string>> &bgdocs,
    std::string_view question) {
  std::unique_ptr<ModelClient> client =
      ModelClient::Create(model, api_key);
  if (client == nullptr) {
    return false;
  }
  client->SetVerbose(verbose);
  client->SetLog(log);

  std::string bg = MakeBackground(bgdocs);

  std::string prompt = std::format(
      R"(Domain: Mathematics.

We are working on an open mathematical problem. Your goal is to provide
mathematical insight, to discover reasoning mistakes and incorrect
assumptions, to make connections with theorems and branches of mathematics,
and provide a brief of the best 4-8 avenues of exploration.

{}
Next, you'll see the specific problem described inside the
<PROBLEM></PROBLEM> tags:

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
      bg, question);

  std::string raw = client->Infer(prompt);

  Print("\n{}\n", raw);
  return true;
}

static bool ConsultPersonas(
    int verbose,
    bool log,
    Model model,
    std::string_view api_key,
    const std::vector<std::pair<std::string, std::string>> &bgdocs,
    std::string_view question) {
  ArcFour rc(std::format("{}.consult", time(nullptr)));

  std::vector<std::string> personas_generic = {
    "a theoretical physicist",
    "a topologist",
    "a category theorist",
    "an algebraist",
    "a continuum mechanician",
    "an expert in homotopy theory",
    "a graph theorist",
    "OpenAI's new frontier model",
  };

  std::vector<std::string> people = {
    "Paul Erdös",
    "Terry Tao",
    "Eric Demaine",
    "Kurt Gödel",
    "Srinivasa Ramanujan",
    "June Huh",
    "Leonhard Euler",
    "Emmy Noether",
    "Grigori Perelman",
    "Andrew Wiles",
    "Maryam Mirzakhani",
    "Martin Hairer",
    "Cédric Villani",
    "Peter Scholze",
    "Richard S. Hamilton",
    "Simon Donaldson",
    "Pierre Deligne",
    "Maxim Kontsevich",
    "Shing-Tung Yau",
    "Mikhail Gromov",
    "Roger Penrose",
  };

  Shuffle(&rc, &people);
  Shuffle(&rc, &personas_generic);

  std::vector<std::string> personas;
  for (int i = 0; i < 2; i++) personas.push_back(people[i]);
  for (int i = 0; i < 2; i++) personas.push_back(personas_generic[i]);

  std::vector<std::string> ideas =
    ParallelMap(personas,
                [&](std::string persona) {
                  return ConsultAs(verbose, log,
                                   model, api_key, bgdocs, question, persona);
                },
                4);

  if (VectorAll(ideas, [](const std::string &s) { return s.empty(); })) {
    return false;
  }

  std::string result =
    std::format("Consultation by {}, {}:\n\n",
                ModelClient::ModelName(model),
                Util::FormatTime("%d %b %Y", time(nullptr)));

  for (size_t i = 0; i < ideas.size(); i++) {
    AppendFormat(&result, "## As {}\n\n{}\n\n", personas[i], ideas[i]);
  }

  Print("{}\n", result);
  return true;
}


#define PROMPT_COLOR ANSI_FG(138, 188, 242)
#define RESP_COLOR ANSI_FG(207, 138, 242)

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  constexpr std::string_view usage =
    "./consult.exe [-bg doc1] [-bg doc2] question-doc.md\n"
    "\n"
    "\n"
    "Use -v to increase verbosity; -log to save the log;\n"
    "and -single to use the old single-persona mode.\n";

  int verbose = 0;
  bool log = false;

  std::optional<std::string> filename;

  std::vector<std::pair<std::string, std::string>> bgdocs;

  Model model = Model::GEMINI_BEST;

  bool personas = true;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg.starts_with("-")) {
      if (arg == "-v") {
        verbose++;
        continue;
      }

      // Use the older single inference mode, rather than
      // asking multiple times with a sample of personas.
      if (arg == "-single") {
        personas = false;
        continue;
      }

      if (arg == "-log") {
        log = true;
        continue;
      }

      if (arg == "-bg") {
        i++;
        CHECK(i < argc) << "-bg needs a filename";
        std::string filename = argv[i];
        std::string content =
          Util::LoseOuterWhitespace(Util::ReadFile(filename));
        CHECK(!content.empty()) << "Background doc " << filename
                                << " is missing or empty";
        bgdocs.emplace_back(std::move(filename), std::move(content));
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

  CHECK(filename.has_value() && !filename.value().empty()) << usage;

  std::string problem;
  if (filename.value() == "-") {
    problem = {Util::ReadStdin()};
  } else {
    CHECK(filename.value()[0] != '-') << usage;
    problem = Util::ReadFile(filename.value());
  }

  problem = Util::LoseOuterWhitespace(problem);

  CHECK(!problem.empty()) << usage << "\n\nNeed a non-empty problem.";

  const std::string api_key = ModelUtil::GetAPIKey();

  if (personas) {
    return ConsultPersonas(verbose, log, model, api_key, bgdocs, problem) ? 0 : 1;
  } else {
    return Consult(verbose, log, model, api_key, bgdocs, problem) ? 0 : 1;
  }
}
