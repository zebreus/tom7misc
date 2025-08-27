
// Like monospace.exe, but processes a whole text file.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"
#include "util.h"
#include "auto-histo.h"
#include "threadutil.h"

#include "llm.h"
#include "models.h"
#include "font-image.h"
#include "line-rephrasing.h"

using namespace std;

static constexpr bool GENERATE_IMAGES = false;

[[maybe_unused]]
static void PrintGreyParity(const std::string &tok) {
  static bool odd = 0;
  if (odd) {
    Print(AFGCOLOR(125, 125, 140, "{}"), tok);
  } else {
    Print(AFGCOLOR(190, 190, 225, "{}"), tok);
  }
  odd = !odd;
}

static std::string LengthIndicator(int line_width) {
  return std::string(line_width, '-');
}

struct MonoFile {

  int line_width = 80;
  std::unique_ptr<BitmapFont> font;
  LLM *llm = nullptr;
  // State right after the shared prompt.
  LLM::State shared_state;

  std::string PromptShared() const {
    return "Exercise in rephrasing text. Following is a series of paragraphs, each of which appears in <P></P> tags. The paragraphs need to be rephrased so that they retain their precise meaning, but with minor variations in the specific choice of words, punctuation, and so on. No new facts should be introduced or removed, but it is good to use synonyms and change the word order and phrasing. They should remain about the same length. After the original paragraphs, the rephrased paragraphs appear.\n\n"
      "Original paragraphs:\n";
  }

  MonoFile(int line_width, LLM *llm) : line_width(line_width), llm(llm) {
    if (GENERATE_IMAGES) {
      font = BitmapFont::Load("../bit7/fixedersys2x.cfg");
    }

    Timer startup_timer;
    llm->Reset();

    std::string shared_prompt = PromptShared();
    Print(AGREY("Shared prompt: [{}]") "\n", shared_prompt);

    llm->DoPrompt(shared_prompt);
    Print("(finished the shared prompt)\n");
    shared_state = llm->SaveState();
  }

  std::vector<std::string> RephraseOneParagraph(
    // Some previous paragraphs
    const std::vector<std::string> &prev,
    // The paragraph to rephrase.
    const string &original) {

    Asynchronously viz_async(8);

    llm->LoadState(shared_state);

    // We already emitted the shared prompt. Next we have the original
    // paragraphs, surrounded with <P></P>, then copies of all but the
    // last paragraph, and then its opening <P>.

    std::string prev_paras;
    for (std::string para : prev) {
      // We want to normalize the paragraphs so that the LLM doesn't
      // get the wrong idea about stuff like double spaces after
      // periods.
      para = Util::NormalizeWhitespace(para);
      AppendFormat(&prev_paras, "<P>{}</P>\n", para);
    }

    std::string task_prompt = prev_paras;

    // Also the original paragraph!
    AppendFormat(&task_prompt, "<P>{}</P>\n",
                 Util::NormalizeWhitespace(original));

    AppendFormat(&task_prompt,
                 "\n\n"
                 "Rephrased paragraphs:\n");

    task_prompt += prev_paras;
    AppendFormat(&task_prompt,
                 "<P>");

    Print("Adding task prompt:\n" AGREY("{}") "\n", task_prompt);

    // Now monospace this one paragraph.
    llm->InsertString(task_prompt, true);

    std::vector<std::string> lines;

    std::unique_ptr<AutoHisto> len_histo;
    auto ResetHisto = [&len_histo]() {
        len_histo.reset(new AutoHisto(1000));
      };
    ResetHisto();

    std::map<std::string, int> failures;

    LLM::State line_beginning = llm->SaveState();

    for (;;) {
      // For each line...
      LineRephrasing lrep(llm, lines.empty(), 3);

      // Try repeatedly to rephrase...
      for (;;) {

        for (const std::string &line : lines) {
          Print(AFGCOLOR(200, 250, 200, "{}") "\n", line);
        }

        for (const auto &[line, times] : failures) {
          Print(AFGCOLOR(190, 50, 50, "{}") "{}\n",
                line.c_str(),
                times > 1 ?
                std::format(" x " AYELLOW("{}"), times) : "");
        }

        Print("{}\n", LengthIndicator(line_width));

        std::string line = lrep.RephraseOnce(
            line_width,
            [this](const std::string &s) {
              Print(ANSI_RESTART_LINE "{}", s);
              return (int)s.size() >= line_width ||
                Util::EndsWith(s, "</P>");
            });

        Print(ANSI_RESTART_LINE AYELLOW("Got:") "\n"
              AWHITE("{}") "\n", line);

        std::string_view last_line = line;
        // We might be done if the line now ends with
        // </P> and fits in the width (*after* stripping!)
        // To be even stricter (full justification), we
        // could change the second condition to ==.
        if (Util::TryStripSuffix("</P>", &last_line) &&
            (int)last_line.size() <= line_width) {
          lines.push_back(std::string(last_line));
          return lines;
        }

        Print("\n(Length: {})\n", line.size());
        len_histo->Observe(line.size());


        if ((int)line.size() == line_width) {
          // Good!
          lines.push_back(line);

          line_beginning = llm->SaveState();
          failures.clear();
          ResetHisto();
          // and continue with the next line.
          break;
        } else {
          Print(ARED("Failed:") "\n"
                "{}\n{}\n",
                LengthIndicator(line_width),
                // std::string(WIDTH, '-'),
                line);

          Print("Histo:\n"
                "{}\n",
                len_histo->SimpleHorizANSI(11));
          failures[line]++;
          // Otherwise, try again.
          llm->LoadState(line_beginning);
        }
      }
    }
  }


  void RephraseTextFile(const std::string &text,
                        const std::string &out_filename) {
    Timer run_timer;
    FILE *outfile = fopen(out_filename.c_str(), "w");
    CHECK(outfile != nullptr) << out_filename;

    // Split the input into paragraphs.
    //
    // Paragraphs can contain line breaks in them, and so
    // we treat more than one consecutive newline as a paragraph
    // break.

    std::vector<std::string> paras;
    std::string cur;
    // The count of consecutive newlines.
    int newline_count = 0;
    for (int i = 0; i < (int)text.size(); i++) {
      char c = text[i];
      if (c == '\r') continue;
      if (c == '\n') {
        newline_count++;
      } else {
        if (newline_count > 1) {
          // flush paragraph.
          paras.push_back(std::move(cur));
          cur.clear();
        } else if (newline_count == 1) {
          // Keep a single newline.
          cur.push_back('\n');
        }
        cur.push_back(c);
        newline_count = 0;
      }
    }

    if (!cur.empty()) paras.push_back(std::move(cur));

    std::vector<std::string> paras_out;
    auto AddParagraph = [&outfile, &paras_out](const std::string &p) {
        paras_out.push_back(p);
        fprintf(outfile, "%s\n\n", p.c_str());
        fflush(outfile);
      };

    for (int i = 0; i < (int)paras.size(); i++) {
      const std::string &para = paras[i];
      const std::string np = Util::NormalizeWhitespace(para);
      if ((int)np.size() <= line_width) {
        // Because we don't require the last line of a
        // paragraph to be the target length, there's nothing to
        // do. Note that we keep whitespace in these, since
        // it is common for something like a title or byline
        // to be centered.
        AddParagraph(para);
      } else {
        std::vector<std::string> prev;
        int start = std::max(0, i - NUM_PREV);
        for (int j = start; j < i; j++) {
          prev.push_back(paras[j]);
        }

        std::vector<std::string> lines = RephraseOneParagraph(prev, para);
        std::string rp = Util::Join(lines, "\n");

        AddParagraph(rp);
      }

      Print("\n{}\n", ANSI::ProgressBar(i + 1, paras.size(),
                                        "Paragraphs",
                                        run_timer.Seconds()));
    }

    fclose(outfile);
    Print("Wrote {} in {}\n", out_filename,
          ANSI::Time(run_timer.Seconds()));
  }


  static constexpr int NUM_PREV = 3;
};

int main(int argc, char ** argv) {
  ANSI::Init();
  CHECK(argc >= 2) << "Usage: ./rephrase.exe len input.txt [output.txt]\n"
    "\nWhere len is the target number of characters per line,\n"
    "and input.txt is an ASCII text file.\n";

  const std::string in_filename = argv[2];
  const std::string input = Util::ReadFile(in_filename);
  CHECK(!input.empty()) << argv[2];

  const std::string out_filename = argc > 3 ? argv[3] :
    std::string(Util::FileBaseOf(in_filename)) + "-justified.txt";

  const int line_width = atoi(argv[1]);
  CHECK(line_width > 0) << "Must provide a positive line width. Got: "
                         << argv[1] << "\n";

  Timer model_timer;

  // Best.
  // ContextParams cparams = Models::LLAMA_70B_F16;
  ContextParams cparams = Models::LLAMA_70B_Q8;
  // Fast.
  // ContextParams cparams = Models::LLAMA_7B_F16;

  SamplerParams sparams;
  // We have our own sampling, so this does nothing.
  sparams.type = SampleType::GREEDY;

  LLM llm(cparams, sparams);
  Print(AGREEN("Loaded model") " in {}.\n",
        ANSI::Time(model_timer.Seconds()));

  MonoFile mono_file(line_width, &llm);

  mono_file.RephraseTextFile(input, out_filename);

  return 0;
}
