
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

static FontDB::Type Reclassify(int thread_idx,
                               std::string_view fontname,
                               FontDB::Type type) {
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
          .text = "Is this image of a normal text font? It should "
          "show the text 'ABCabc'. If you see drawings (even drawings "
          "that contain letters), boxes indicating missing letters, "
          "or text other than 'ABCabc', then this is not a normal "
          "text font. Small caps are acceptable.\n"

          "First give your reasoning in one sentence, and then one of "
          "these two specific codes on a separate line by itself:\n"

          "NORMAL - A good clean font, like a sans-serif font or a "
          "serif font; normal-looking Roman letterforms ABCabc with "
          "good construction. These fonts can be somewhat decorative "
          "if they would be appropriate for book titles, but should "
          "not have exotic, edgy, or messy letterforms like those "
          "in homemade fonts.\n"

          "WEIRD - A broken font, a font with missing glyphs, or with "
          "glyphs that are not the letters ABCabc; fonts that contain "
          "drawing or dingbats (even if those drawings contain letters); "
          "extremely decorative or fancy fonts; fonts with special effects; "
          "fonts that rendered incorrectly or are far too big or too "
          "small.\n"
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


  if (type_str == "WEIRD") type = FontDB::Type::UNKNOWN;
  else if (type_str == "NORMAL") {
    // Nothing; keep the existing type.
  } else {
    status->Print("Unparseable result " ARED("{}") "\n",
                  type_str);
    type = FontDB::Type::UNKNOWN;
  }

  if (!res.reasoning_content.empty()) {
    if (reasoning.empty()) reasoning = res.reasoning_content;
    else reasoning = res.reasoning_content + "\n" + reasoning;
  }

  status->Print(AWHITE("{}") " " ACYAN("{}") "\n"
                AGREY("{}") "\n",
                fontname, FontDB::TypeString(type),
                reasoning);

  if (type == FontDB::Type::UNKNOWN) {
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
        img.Save(std::format("redebug/{}.png", img_filename));
      });
  }

  return type;
}

static void ClassifyMany() {
  ArcFour rc(std::format("classify.{}", time(nullptr)));
  SetAction(0, "Load DB");
  std::unique_ptr<FontDB> db = FontDB::Create();

  int64_t already = 0;

  const std::unordered_map<std::string, FontDB::Info> &files = db->Files();

  std::vector<std::pair<std::string, FontDB::Type>> todo;
  for (const auto &[name, info] : files) {
    // Only revisit machine labels.
    auto it = info.flags.find(FontDB::Flag::GEMMA_LABEL);
    bool gemma = it != info.flags.end() && it->second;
    if (gemma &&
        // Types we want to be particularly clean.
        (info.type == FontDB::Type::SANS ||
         info.type == FontDB::Type::SERIF)) {
      todo.push_back(std::make_pair(name, info.type));
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
          std::pair<std::string, FontDB::Type> row;
          {
            MutexLock ml(&m);
            if (next_idx >= todo.size()) {
              SetAction(thread_idx, "Done");
              return;
            }

            row = todo[next_idx];
            next_idx++;
          }

          FontDB::Type type = Reclassify(thread_idx, row.first, row.second);
          db->AssignType(row.first, type);
          db->SetFlag(row.first, FontDB::Flag::GEMMA_LABEL, true);
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
