
// Like monospace.exe, but processes a whole text file.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

static constexpr bool GENERATE_IMAGES = false;
// 5% seems like a good choice, but I'm using lower for the
// AEOUD talk visualization.
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
#define ASCII_CHAR "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?/,.'\"\\[\\]\\\\ ]"
#define ASCII_NOT_SPACE "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?/,.'\"\\[\\]\\\\]"

// TODO: Increase weights of tokens from input.

// Tokens are not typically full words. As we fill a line,
// we make sure that we add a word at a time, so that we
// don't inadvertently end a line in the middle of a word.
struct WordStream {
  // The wordstream is a lightweight wrapper around the llm;
  // it does not maintain any state.
  explicit WordStream(int max_word_length, LLM *llm) :
    max_word_length(max_word_length), llm(llm) {}

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
      if ((int)partial_word.size() > max_word_length) return partial_word;

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

      if (!partial_word.empty() &&
          (id == llm->context.EOSToken() ||
           tok[0] == ' ')) {
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
        // printf("Partial word [%s]\n", partial_word.c_str());
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
  int max_word_length = 80;
  LLM *llm = nullptr;
};

static std::string LengthIndicator(int line_width) {
  return std::string(line_width, '-');
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
    printf(AGREY("Shared prompt: [%s]") "\n", shared_prompt.c_str());

    llm->DoPrompt(shared_prompt);
    printf("(finished the shared prompt)\n");
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
      StringAppendF(&prev_paras, "<P>%s</P>\n", para.c_str());
    }

    std::string task_prompt = prev_paras;

    // Also the original paragraph!
    StringAppendF(&task_prompt, "<P>%s</P>\n",
                  Util::NormalizeWhitespace(original).c_str());

    StringAppendF(&task_prompt,
                  "\n\n"
                  "Rephrased paragraphs:\n");

    task_prompt += prev_paras;
    StringAppendF(&task_prompt,
                  "<P>");

    printf("Adding task prompt:\n" AGREY("%s") "\n", task_prompt.c_str());

    // Now follow the classic monospace approach.
    llm->InsertString(task_prompt, true);

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

        viz_async.Run([this, frame = std::move(frame)]() {
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
                          font_width * line_width,
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
      printf("%s\n", LengthIndicator(line_width).c_str());

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

      WordStream word_stream(line_width, llm);

      // std::unordered_map<std::string, int> prefix_count;

      // Sample words for this line.
      while ((int)line.size() < line_width) {
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
      }

      printf("\n(Length: %d)\n", (int)line.size());

      len_histo->Observe(line.size());

      if ((int)line.size() == line_width) {
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
               LengthIndicator(line_width).c_str(),
               // std::string(WIDTH, '-').c_str(),
               line.c_str());
        // Don't increase temperature as long as we are
        // getting some variety.
        // current_temp += 0.0125;

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

      printf("\n%s\n", ANSI::ProgressBar(i + 1, paras.size(),
                                         "Paragraphs",
                                         run_timer.Seconds()).c_str());
    }

    fclose(outfile);
    printf("Wrote %s in %s\n", out_filename.c_str(),
           ANSI::Time(run_timer.Seconds()).c_str());
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
  printf(AGREEN("Loaded model") " in %s.\n",
         ANSI::Time(model_timer.Seconds()).c_str());

  MonoFile mono_file(line_width, &llm);

  mono_file.RephraseTextFile(input, out_filename);

  return 0;
}
