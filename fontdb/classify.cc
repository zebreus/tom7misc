
#include <array>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "font-db.h"
#include "fonts/ttf.h"
#include "image.h"
#include "net.h"
#include "periodically.h"
#include "randutil.h"
#include "spark-infer.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

DECLARE_COUNTERS(ctr_bad_response, ctr_done_here);

static constexpr std::string_view HOST = "10.0.0.34";
static constexpr int PORT = 8080;

static constexpr int NUM_THREADS = 16;
static constexpr int SHEET_WIDTH = 900;
static constexpr int SHEET_HEIGHT = 300;

static StatusBar *status = nullptr;
static Asynchronously *async = nullptr;

static std::mutex action_mutex;
static std::array<std::string, NUM_THREADS> actions;

static void SetAction(int thread_idx, std::string_view action) {
  MutexLock ml(&action_mutex);
  actions[thread_idx] = action;
  std::string line = ANSI_BG(0, 0, 80) ANSI_WHITE "══╡ ";
  for (int i = 0; i < NUM_THREADS; i++) {
    if (i != 0) line += ANSI_WHITE " | ";
    AppendFormat(&line, ANSI_GREEN "{}", actions[i]);
  }
  line += ANSI_WHITE " ╞══════════════════════════════"
    "═══════════════════════════════════════════" ANSI_RESET;

  status->LineStatus(0, "{}", line);
}

std::optional<ImageRGBA> FontSheet(std::string_view fontname) {
  std::unique_ptr<TTF> ttf = TTF::Load(fontname);
  if (ttf.get() == nullptr || ttf->FontInfo()->numGlyphs == 0) {
    return std::nullopt;
  }

  ImageRGBA img(SHEET_WIDTH, SHEET_HEIGHT);
  img.Clear32(0xFFFFFFFF);

  ttf->BlitStringFloat(25.0f, SHEET_HEIGHT - 100.0f,
                       SHEET_HEIGHT - 125.0f, "ABCabc",
                       [&img](int x, int y, uint8_t v) {
                         img.BlendPixel32(x, y, 0x00000000 | v);
                       }, true);

  return img;
}

static double render_seconds = 0.0;
static double infer_seconds = 0.0;

static FontDB::Type Classify(int thread_idx, std::string_view fontname) {
  SetAction(thread_idx, "Render");
  Timer render_timer;
  std::optional<ImageRGBA> sheet = FontSheet(fontname);
  render_seconds += render_timer.Seconds();
  if (!sheet.has_value()) {
    status->Print("Couldn't load " ARED("{}") "\n", fontname);
    return FontDB::Type::UNKNOWN;
  }

  ImageRGBA img = std::move(sheet.value());

  SetAction(thread_idx, "Infer");
  Spark spark(HOST, PORT);

  Spark::ModelRequest req;
  req.instructions = "Think fast; time is of the essence!";
  req.messages = {
    Spark::ReqMessage{
      .role = "user",
      .content = {
        Spark::TextChunk{
          .text = "Can you describe the style of the font "
          "in this image? It shows the text 'ABCabc' rendered with "
          "the font. "
          "Different weights and italics are OK; interpret this as "
          "a question about the inferred font family.\n"

          "First give your "
          "reasoning in one sentence, and then one of these specific "
          "types, in all caps, on its own line:\n"

          /*
          "Answer with just one of these specific types, in all caps, "
          "on its own line:\n"
          */

          "SANS - Good clean sans-serif font, which has normal-looking "
          "letters with good construction, and no effects like outlines.\n"

          "SERIF - Good clean serif font, which has normal-looking "
          "letters with good construction, and no effects like outlines.\n"

          "FANCY - Fanciful but precise letterforms, including cursive, "
          "blackletter, and caligraphic styles.\n"

          "TECHNO - Cyber-fonts, pixelated bitmap fonts, and similar.\n"

          "DECORATIVE - Clean fonts that are not \"fancy\" or \"techno\" "
          "but have some other decorative style that makes the letter "
          "shapes not be normal (e.g. \"old west\" font). Only applicable "
          "if the glyphs depict the letters ABCabc.\n"

          "MESSY - Scans, handwriting, sloppy, or distressed fonts with "
          "lots of control points.\n"

          "DINGBATS - The font glyphs are not actually letters; they're "
          "icons or drawings. You must choose this style if the glyphs "
          "render but do not read ABCabc.\n"

          "OTHER - The font rendered correctly, but doesn't match "
          "the above styles. Anything with 'effects,' where the "
          "letterforms are not solid strokes (like for example they "
          "are outlines, in circles, have shadows, or are shaded) goes "
          "here.\n"

          "BROKEN - The image is blank, some of the glyphs display "
          "as missing or boxes, or something is wrong "
          "with the rendering. Text exceeding the image bounds a small "
          "amount is not considered broken.\n",
        },
        Spark::ImageChunk{
          .img = img,
        },
      }
    },
  };

  Timer infer_timer;
  Spark::ModelResponse res = spark.Infer(req, 0);
  infer_seconds += infer_timer.Seconds();
  CHECK(res.error.empty()) << res.error;

  std::string reasoning;
  FontDB::Type type = FontDB::Type::UNKNOWN;

  std::string_view s = res.content;
  Util::RemoveOuterWhitespace(&s);

  size_t nl = s.find_last_of('\n');
  std::string_view type_str;
  if (nl != std::string_view::npos) {
    std::string_view r = s.substr(0, nl);
    Util::RemoveTrailingWhitespace(&r);
    reasoning = std::string(r);
    type_str = s.substr(nl + 1);
  } else {
    type_str = s;
  }

  auto TypePrefixChar = [](char c) {
    return Util::IsWhitespace(c) ||
           c == '*' || c == '.' || c == '"' || c == '\'';
  };
  while (!type_str.empty() && TypePrefixChar(type_str.front())) {
    type_str.remove_prefix(1);
  }
  while (!type_str.empty() && TypePrefixChar(type_str.back())) {
    type_str.remove_suffix(1);
  }

  if (type_str == "SANS") type = FontDB::Type::SANS;
  else if (type_str == "SERIF") type = FontDB::Type::SERIF;
  else if (type_str == "FANCY") type = FontDB::Type::FANCY;
  else if (type_str == "TECHNO") type = FontDB::Type::TECHNO;
  else if (type_str == "DECORATIVE") type = FontDB::Type::DECORATIVE;
  else if (type_str == "MESSY") type = FontDB::Type::MESSY;
  else if (type_str == "DINGBATS") type = FontDB::Type::DINGBATS;
  else if (type_str == "OTHER") type = FontDB::Type::OTHER;
  else if (type_str == "BROKEN") type = FontDB::Type::BROKEN;
  else {
    ctr_bad_response++;
  }

  if (!res.reasoning_content.empty()) {
    if (reasoning.empty()) reasoning = res.reasoning_content;
    else reasoning = res.reasoning_content + "\n" + reasoning;
  }

  status->Print(AWHITE("{}") " " ACYAN("{}") "\n"
                AGREY("{}") "\n",
                fontname, FontDB::TypeString(type),
                reasoning);

  async->Run(
      [fontname = std::string(fontname),
       img = std::move(img),
       reasoning = std::move(reasoning),
       type]() mutable {
      img.BlendText32(6, 6, 0x000077FF,
                      FontDB::TypeString(type));
      img.BlendText32(6, 18, 0x777777FF,
                      reasoning);

      std::string img_filename = Util::FileOf(fontname);
      (void)Util::TryStripSuffix(".ttf", &img_filename);
      img_filename = Util::Replace(img_filename, " ", "_");
      img.Save(std::format("debug/{}.png", img_filename));
    });

  return type;
}

static void ClassifyMany() {
  ArcFour rc(std::format("classify.{}", time(nullptr)));
  SetAction(0, "Load DB");
  std::unique_ptr<FontDB> db = FontDB::Create();

  int64_t already = 0;

  const std::unordered_map<std::string, FontDB::Info> &files = db->Files();

  std::vector<std::string> todo;
  for (const auto &[name, info] : files) {
    if (info.type == FontDB::Type::UNKNOWN) {
      todo.push_back(name);
    } else {
      already++;
    }
  }

  Shuffle(&rc, &todo);

  status->Print("{} already done. {} left to do.\n",
                already, todo.size());

  std::mutex m;
  size_t next_idx = 0;
  Timer timer;
  Periodically save_per(60.0, false);
  Periodically status_per(1.0);
  ParallelFan(
      NUM_THREADS,
      [&](int thread_idx) {
        for (;;) {
          std::string filename;
          {
            MutexLock ml(&m);
            if (next_idx >= todo.size()) {
              SetAction(thread_idx, "Done");
              return;
            }

            filename = todo[next_idx];
            next_idx++;
          }

          FontDB::Type type = Classify(thread_idx, filename);
          db->AssignType(filename, type);
          db->SetFlag(filename, FontDB::Flag::GEMMA_LABEL, true);
          ctr_done_here++;

          save_per.RunIf([&]{
              SetAction(thread_idx, "Saving");
              db->Save(false);
              status->Print(AYELLOW("Saved") ".\n");
            });

          status_per.RunIf([&]{
              int64_t done_here = ctr_done_here.Read();
              double total_time = timer.Seconds();
              double time_each = total_time / done_here;
              status->LineStatus(
                  1, "{} done here, {} already, "
                  "{} left ({}; {} ea.) "
                  "{:2.2f}% inference",
                  done_here, already, todo.size() - done_here,
                  ANSI::Time(total_time),
                  ANSI::Time(time_each),
                  (infer_seconds * 100.0) / total_time);
            });
        }
      });

  status->Print("All done.\n");
}



int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  status = new StatusBar(2);
  async = new Asynchronously(NUM_THREADS + 2);

  ClassifyMany();

  delete async;

  status->Remove();

  Print("OK\n");
  return 0;
}
