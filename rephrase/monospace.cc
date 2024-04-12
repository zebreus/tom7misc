
// Simple greedy experiment; fixed widths.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"
#include "util.h"
#include "auto-histo.h"
#include "threadutil.h"
#include "image.h"
#include "color-util.h"

#include "llama.h"
#include "llm.h"
#include "models.h"
#include "font-image.h"

using namespace std;

// XXX command-line flag
static constexpr int LINE_WIDTH = 41;

static constexpr bool GENERATE_IMAGES = true;
// 5% seems like a good choice, but I'm using lower for the
// AEOUD talk visualization.
// static constexpr float MIN_P = 0.05;
static constexpr float MIN_P = 0.05;

static void PrintGreyParity(const std::string &tok) {
  static bool odd = 0;
  if (odd) {
    printf(AFGCOLOR(125, 125, 140, "%s"), tok.c_str());
  } else {
    printf(AFGCOLOR(190, 190, 225, "%s"), tok.c_str());
  }
  odd = !odd;
}

// Characters that it's reasonable to start a line with.
#define ASCII_LINE_START "[A-Za-z0-9()\"']"
#define ASCII_CHAR "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\ ]"
#define ASCII_NOT_SPACE "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?,.'\"\\[\\]\\\\]"

// TODO: Increase weights of tokens from input.

// Tokens are not typically full words. As we fill a line,
// we make sure that we add a word at a time, so that we
// don't inadvertently end a line in the middle of a word.
struct WordStream {
  explicit WordStream(LLM *llm) : llm(llm) {
  }

  llama_token Sample(float temp,
                     std::vector<std::pair<std::string, float>> *next) {
    auto cand = llm->context.GetCandidates();
    llm->sampler.FilterByNFA(cand.get());
    Sampler::UpdateCandidatesTemp(temp, cand.get());

    // Generate visualization before min_p sampling.
    if (next != nullptr) {
      auto top = llm->TopCandidates(*cand, -1);
      for (const auto &[s, l, p] : top) {
        next->emplace_back(s, p);
      }
    }

    if (MIN_P > 0.0) {
      llm->sampler.UpdateCandidatesMinP(MIN_P, 1, cand.get());
    }

    return llm->sampler.SampleRaw(std::move(cand));
  }

  // Returns the word (whitespace etc. intact).
  std::string Next(float temp,
                   std::vector<std::pair<std::string, float>> *next_out) {

    CHECK(!llm->sampler.Stuck()) << "STUCK!";

    // We start with the probability distribution just being the
    // empty string with 1.0 mass.
    std::map<std::string, float> wnext = {
      {"", 1.0},
    };

    std::string partial_word;
    for (;;) {
      // We will definitely fail in this case (and probably
      // the model is off the rails), so just return it.
      if (partial_word.size() > LINE_WIDTH) return partial_word;

      // Sample, but do not yet take, the token.
      // int id = llm->Sample();
      std::vector<std::pair<std::string, float>> next;
      const int id = Sample(temp, &next);
      string tok = llm->context.TokenString(id);

      // We have a "word" if the new partial_word contains a space.
      //
      // Since spaces only lead tokens, this happens when the newly
      // predicted token starts with a space. And then the save state
      // is the one right before this token.
      //
      // Note that the "word" here could be something that ends a
      // sentence, like "finished." or "\"finishing,\"". Basically we
      // rely on the model's own notion of word breaks.
      //
      // TODO: We could insist that the next token is appropriate
      // for after a line break (e.g. it should not be " --").

      if (!partial_word.empty() && tok[0] == ' ') {
        // PERF: We could keep the sampled token for the next
        // call, if we want, saving a little time.
        if (next_out != nullptr) {
          next_out->clear();
          next_out->reserve(wnext.size());
          for (const auto &[s, p] : wnext) {
            next_out->emplace_back(s, p);
          }
          // By descending probability.
          std::sort(next_out->begin(), next_out->end(),
                    [](const auto &a, const auto &b) {
                      return a.second > b.second;
                    });
        }
        return partial_word;
      } else {

        // Now split the current word into multiple entries.
        {
          float oldp = wnext[partial_word];
          wnext.erase(partial_word);
          for (const auto &[suffix, p] : next) {
            std::string new_word = partial_word + suffix;
           float new_p = oldp * p;
            // In theory this could coincide with an existing entry,
            // so add probability mass in that case.
            wnext[new_word] += new_p;
          }
        }

        // Otherwise, keep building up the word.
        // We accept the token.
        llm->TakeTokenBatch({id});
        partial_word += tok;
      }
    }
  }
private:
  LLM *llm = nullptr;
};

static std::string LengthIndicator() {
  return std::string(LINE_WIDTH, '-');
}

struct VizFrame {
  int frame_num = 0;
  AutoHisto::Histo length_histo;
  // Successful lines so far
  std::vector<std::string> good;
  float temp = 0.0;
  std::map<std::string, int> failures;

  // Line so far.
  std::string current;
  std::vector<std::pair<std::string, float>> nexts;
};

static void RephraseMono(
    LLM *llm, const string &prompt, const string &original) {
  printf("Loaded prompt of %d chars\n", (int)prompt.size());

  Asynchronously viz_async(8);
  std::unique_ptr<BitmapFont> font;
  if (GENERATE_IMAGES) {
    font = BitmapFont::Load("../bit7/fixedersys2x.cfg");
  }

  Timer startup_timer;
  llm->Reset();

  // Prompt is instructions + original text + header:
  std::string full_prompt =
    StringPrintf("%s\n"
                 "\n"
                 "Original text:\n\n"
                 "%s\n"
                 "\n"
                 "Rephrased text:\n\n",
                 prompt.c_str(), original.c_str());

  printf(AGREY("Full prompt: [%s]") "\n", full_prompt.c_str());

  llm->DoPrompt(full_prompt);
  printf("(finished the prompt)\n");

  // Now we greedily re-insert text.
  // For this version, we don't even try to preserve lines.
  // So we just want to keep retrying until we have a word
  // ending at exactly WIDTH.

  std::vector<std::string> lines;

  std::unique_ptr<AutoHisto> len_histo;
  auto ResetHisto = [&len_histo]() {
      len_histo.reset(new AutoHisto(1000));
    };
  ResetHisto();

  std::map<std::string, int> failures;

  LLM::State line_beginning = llm->SaveState();
  float current_temp = 1.0f;
  int frame_num = 0;

  auto GenFrame = [&](const std::string &line,
                      std::vector<std::pair<std::string, float>> *nexts) {
    if (GENERATE_IMAGES) {
      VizFrame frame{
        .frame_num = frame_num++,
        .length_histo = len_histo->GetHisto(12),
        .good = lines,
        .temp = current_temp,
        .failures = failures,
        .current = line,
      };

      if (nexts != nullptr) {
        frame.nexts = *nexts;
      }

      viz_async.Run([&font, frame = std::move(frame)]() {
        const int WIDTH = 960;
        const int HEIGHT = 540;

        ImageRGBA img(WIDTH, HEIGHT);
        img.Clear32(0x000000FF);

        int xpos = 8;
        int ypos = 8;

        font->DrawText(&img, xpos, ypos,
                       0x999999FF,
                       "Rephrased text:");
        ypos += font->Height();

        // Fixed width.
        const int font_width = font->Width('m');

        for (int y = 0; y < (int)frame.good.size(); y++) {
          font->DrawText(&img, xpos, ypos,
                         0x99FF99FF,
                         frame.good[y]);
          ypos += font->Height();
        }

        img.BlendRect32(xpos, ypos,
                        font_width * LINE_WIDTH,
                        font->Height(),
                        0x00000077FF);

        font->DrawText(&img, xpos, ypos,
                       0xFFFFFFCC,
                       frame.current.c_str());
        ypos += font->Height();

        const int bottom_pos = HEIGHT / 2 - font->Height();

        for (const auto &[failed, times] : frame.failures) {
          if (ypos + font->Height() >= bottom_pos)
            break;
          font->DrawText(&img, xpos, ypos,
                         0xCC3333FF,
                         failed);
          if (times > 1) {
            int xx = xpos + failed.size() * font_width;
            font->DrawText(&img, xx, ypos, 0x77770099, " × ");
            xx += 3 * font_width;
            font->DrawText(&img, xx, ypos, 0x997700FF,
                           StringPrintf("%d", times).c_str());
          }

          ypos += font->Height();
        }

        ypos = HEIGHT / 2 - font->Height();

        font->DrawText(
            &img, xpos, ypos,
            ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT,
                                        frame.temp - 1.0),
            // FIRE
            StringPrintf("🔥 %.3f", frame.temp).c_str());
        ypos += font->Height();

        for (const auto &[s, p] : frame.nexts) {
          uint32_t color =
            ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT, p);
          color |= 0xFF;
          font->DrawText(&img, xpos, ypos,
                            color,
                            StringPrintf("%.1f%%", p * 100.0));
          font->DrawText(&img, xpos + 7 * font_width,
                            ypos,
                            color,
                            s);
          ypos += font->Height();
        }

        img.ScaleBy(2).Save(
            StringPrintf("monospace/frame%d.png", frame.frame_num));
      });
    }
    };

  for (;;) {

    for (const std::string &line : lines) {
      printf(AFGCOLOR(200, 250, 200, "%s") "\n", line.c_str());
    }

    for (const auto &[line, times] : failures) {
      printf(AFGCOLOR(190, 50, 50, "%s") "%s\n",
             line.c_str(),
             times > 1 ?
             StringPrintf(" x " AYELLOW("%d"), times).c_str() : "");
    }

    if (current_temp > 1.0f) {
      printf("Temperature: %.4f\n", current_temp);
    }
    printf("%s\n", LengthIndicator().c_str());

    // As a loop invariant, line_beginning is the current state.
    std::string line;
    llm->NewRNG();
    // No newlines allowed, either.

    const bool first_line = lines.empty();
    // must start with non-space. We also exclude various
    // punctuation that wouldn't make sense.
    string first_char_regex;
    if (first_line) {
      first_char_regex = ASCII_LINE_START;
    } else {
      // But! For lines after the first, we want the LLM to think
      // it's inserting a space, since we did insert a line break.
      //
      // We then require a line-starting character.
      first_char_regex = " " ASCII_LINE_START;
    }

    llm->sampler.SetRegEx(
        first_char_regex +
        // Then we can have anything except two spaces in a row,
        // and we can't end with space.
        "(" ASCII_NOT_SPACE "| " ASCII_NOT_SPACE ")*");

    WordStream word_stream(llm);

    // Sample words for this line.
    while (line.size() < LINE_WIDTH) {
      std::vector<std::pair<std::string, float>> next;
      string word =
        word_stream.Next(current_temp,
                         GENERATE_IMAGES ? &next : nullptr);

      // At the beginning of a continuation line, a space was
      // covered by the newline that we implicitly have (and
      // which the LLM does not see).
      if (!first_line && line.empty()) {
        word = Util::LoseWhiteL(word);
      }
      line += word;
      PrintGreyParity(word);
      GenFrame(line, &next);
    }

    printf("\n(Length: %d)\n", (int)line.size());

    len_histo->Observe(line.size());

    if (line.size() == LINE_WIDTH) {
      // Good!
      lines.push_back(line);

      line_beginning = llm->SaveState();
      failures.clear();
      current_temp = 1.0;
      ResetHisto();
      // and continue with the next line.

    } else {
      printf(ARED("Failed:") "\n"
             "%s\n%s\n",
             LengthIndicator().c_str(),
             // std::string(WIDTH, '-').c_str(),
             line.c_str());
      current_temp += 0.0125;

      printf("Histo:\n"
             "%s\n",
             len_histo->SimpleHorizANSI(11).c_str());

      // Whenever we get a repeat, increase temperature so
      // that we are getting more random samples.
      if (failures[line] > 0) {
        current_temp += 0.0125;
      }
      failures[line]++;

      // Otherwise, try again.
      llm->LoadState(line_beginning);
    }

    GenFrame("", nullptr);

  }
}

int main(int argc, char ** argv) {
  CHECK(argc >= 2) << "Usage: ./rephrase.exe input.txt\n";

  constexpr const char *prompt_file = "rephrase-monospace.txt";
  const string prompt = Util::ReadFile(prompt_file);
  CHECK(!prompt.empty()) << prompt_file;

  const std::string input = Util::ReadFile(argv[1]);
  CHECK(!input.empty()) << argv[1];

  // No newlines in original.
  std::string original =
    Util::NormalizeWhitespace(Util::Replace(input, "\n", " "));

  // AnsiInit();
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
  printf(AGREEN("Loaded model") ".\n");

  RephraseMono(&llm, prompt, original);

  return 0;
}
