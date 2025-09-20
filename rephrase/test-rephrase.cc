
// This next experiment rephrases the entire text, and
// then computes a loss function over it. Rather than
// sample randomly, we sample according to the probability
// distribution.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "gtl/top_n.h"
#include "llm.h"
#include "timer.h"
#include "util.h"

using namespace std;

// Characters that it's reasonable to start a line with.
#define ASCII_LINE_START "[A-Za-z0-9()\"']"
#define ASCII_CHAR "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\ ]"
#define ASCII_NOT_SPACE "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\]"

// Alternative path.
struct Alt {
  LLM::State state;
  std::string prefix;
  std::string chosen;
  std::string other;
  int chosen_id = 0;
  int other_id = 0;
  float chosen_p = 0.0;
  float other_p = 0.0;
  double psum = 0.0;
  int pnum = 0;
};

struct AltGreater {
  bool operator()(const Alt &a, const Alt &b) const {
    if (a.other_p > b.other_p) return true;
    if (a.other_p < b.other_p) return false;
    return a.prefix.size() > b.prefix.size();
  }
};

static string TruncTo(size_t max_size, const std::string &text) {
  return text.size() > max_size ?
    text.substr(text.size() - max_size, string::npos) :
    text;
}

static string ColorProb(float prob) {
  const auto &[r, g, b, a_] =
    ColorUtil::Unpack32(
        ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT, prob));

  return std::format("{}{:.4f}%" ANSI_RESET,
                     ANSI::ForegroundRGB(r, g, b),
                     prob * 100.0);
}

struct Rephrasing {
  using TopAlt = gtl::TopN<Alt, AltGreater>;
  LLM *llm;
  TopAlt top;
  const string &prompt, &original;

  Rephrasing(LLM *llm, const string &prompt, const string &original) :
    llm(llm), top(10), prompt(prompt), original(original) {

    Print("Loaded prompt of {} chars\n", prompt.size());

    Timer startup_timer;
    llm->Reset();

    // Prompt is instructions + original text + header:
    std::string full_prompt =
      std::format("{}\n"
                  "\n"
                  "Original text:\n\n"
                  "{}\n"
                  "\n"
                  "Rephrased text:\n\n",
                  prompt, original);

    Print(AGREY("Full prompt: [{}]") "\n", full_prompt);

    llm->DoPrompt(full_prompt);
    Print("(finished the prompt)\n");
    Print("Startup: {}\n", ANSI::Time(startup_timer.Seconds()));
  };

  struct Result {
    std::string text;
    double psum = 0.0;
    int pnum = 0;
    double sec = 0.0;
  };

  std::vector<Result> results;

  void WriteResult(const Result &res) {
    FILE *f = fopen("results.txt", "a");
    Print(f,
          "psum {:.4f}, pnum {}, avg {:.4f}%, {:.1f} sec:\n"
          "{}\n\n",
          res.psum, res.pnum, (100.0 * res.psum) / res.pnum,
          res.sec, res.text);
    fclose(f);
  }

  void Rephrase() {
    Timer all_time;
    // First, run a greedy pass to initialize.
    Print(AYELLOW("Greedy pass...") "\n");
    std::unique_ptr<std::vector<Alt>> alts = Complete("", 0.0, 0);

    // Now run all the top alternatives.
    // XXX this is just an experiment to see what it looks like;
    // we should obviously be merging these!
    for (int i = 0; i < (int)alts->size(); i++) {
      const Alt &alt = (*alts)[i];
      Print(AWHITE("[{}/{}]") " "
            AGREEN("Continuing") " "
            AGREY("{}") " " APURPLE("{}") "..." "\n",
            i + 1, alts->size(),
            TruncTo(50, alt.prefix),
            alt.other);
      // Restore and take the alternate token.
      llm->LoadState(alt.state);
      std::string text = alt.prefix + alt.other;
      llm->TakeTokenBatch({alt.other_id});
      (void)Complete(std::move(text),
                     alt.psum + alt.other_p,
                     alt.pnum + 1);
    }

    Print("Finished {}+1 in {}\n", alts->size(),
          ANSI::Time(all_time.Seconds()));
  }

  // With the llm in some partially-predicted state (starting with text,
  // and with the given psum, pnum), finish the paragraph.
  std::unique_ptr<std::vector<Alt>> Complete(
      std::string text, double psum, int pnum) {
    // Now just complete, greedily.
    Timer complete_timer;
    for (;;) {
      std::unique_ptr<LLM::Candidates> cand = llm->context.GetCandidates();
      llm->AnsiPrintCandidates(*cand, 4);

      // XXX expose this in llm interface
      llama_sample_softmax(llm->context.GetLlamaContext(),
                           &cand->ltda);

      int top_id = (*cand)[0].id;

      // I think this fails on duplicate words because of repetition
      // penalties?
      if (false) {
        const int sampled = llm->Sample();
        CHECK(top_id == sampled)
          << top_id << " vs " << sampled
          << ": " << llm->context.TokenString(top_id)
          << " vs " << llm->context.TokenString(sampled);
      }
      string top_tok = llm->context.TokenString(top_id);

      int other_id = (*cand)[1].id;
      string other_tok = llm->context.TokenString(other_id);

      double chosen_p = (*cand)[0].p;
      double other_p = (*cand)[1].p;

      // The save state is expensive, so only save if it's currently
      // one of the top best.
      if (top.empty() || other_p > top.peek_bottom().other_p) {
        Alt alt;
        alt.state = llm->SaveState();
        alt.prefix = text;
        alt.chosen = top_tok;
        alt.other = other_tok;
        alt.chosen_id = top_id;
        alt.other_id = other_id;
        alt.chosen_p = chosen_p;
        alt.other_p = other_p;
        alt.psum = psum;
        alt.pnum = pnum;
        top.push(std::move(alt));
      }

      constexpr int MAX_PREFIX = 60;
      Print("{}" ABLUE("{}") "\n",
            TruncTo(MAX_PREFIX, text),
            top_tok);

      text += top_tok;

      psum += chosen_p;
      pnum++;

      llm->TakeTokenBatch({top_id});
      if (top_id == llm->context.EOSToken() ||
          top_id == llm->context.NewlineToken()) {
        Print("Done.");
        break;
      }
    }

    const double sec = complete_timer.Seconds();
    Print("\n{}\n", text);
    Print("pnum {}, psum {:.3f}, avg {}. Took {}\n",
          pnum, psum, ColorProb(psum / pnum),
          ANSI::Time(sec));

    Result result;
    result.text = text;
    result.psum = psum;
    result.pnum = pnum;
    result.sec = sec;
    WriteResult(result);
    results.push_back(std::move(result));


    Print("Top alternatives:\n");
    std::unique_ptr<std::vector<Alt>> v(top.Extract());
    // Leave it in a valid state.
    top.Reset();

    for (const Alt &alt : *v) {
      Print(AGREY("{}") "\n", TruncTo(50, alt.prefix));
      Print("  " ABLUE("{}") " ({}) "
            "or " APURPLE("{}") " ({})\n",
            alt.chosen, ColorProb(alt.chosen_p),
            alt.other, ColorProb(alt.other_p));
    }

    return v;
  }

};



int main(int argc, char ** argv) {
  CHECK(argc >= 2) << "Usage: ./rephrase.exe input.txt\n";

  constexpr const char *prompt_file = "rephrase.txt";
  const string prompt = Util::ReadFile(prompt_file);
  CHECK(!prompt.empty()) << prompt_file;

  const std::string input = Util::ReadFile(argv[1]);
  CHECK(!input.empty()) << argv[1];

  // No newlines in original.
  std::string original =
    Util::NormalizeWhitespace(Util::Replace(input, "\n", " "));

  // ANSI::Init();
  Timer model_timer;

  ContextParams cparams;
  // cparams.model = "../llama/models/7B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/7B/ggml-model-f16.bin";
  // cparams.model = "../llama/models/7B/ggml-model-q8_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q4_0.bin";
  // cparams.model = "../llama/models/65B/ggml-model-q8_0.bin";
  // cparams.model = "e:\\llama2\\7b\\ggml-model-q4_0.gguf";
  cparams.model = "e:\\llama2\\70b\\ggml-model-q8_0.gguf";
  // cparams.model = "e:\\llama2\\70b\\ggml-model-f16.gguf";

  // cparams.model = "codellama2/34b/ggml-model-f16.gguf";

  SamplerParams sparams;
  // cparams.mirostat = 2;
  // Probably should use something like "minimum probability" sampling
  // here.
  sparams.type = SampleType::GREEDY;
  sparams.min_p = 0.05;
  sparams.regex = ".*";

  LLM llm(cparams, sparams);
  Print(AGREEN("Loaded model") ".\n");

  Rephrasing rephrasing(&llm, prompt, original);
  rephrasing.Rephrase();
  // Rephrase(&llm, prompt, original);

  return 0;
}
