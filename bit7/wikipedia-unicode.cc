
#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "periodically.h"
#include "threadutil.h"
#include "timer.h"
#include "utf8.h"
#include "util.h"
#include "wikipedia.h"

#include "font-image.h"

using namespace std;

[[maybe_unused]]
static constexpr const char *WIKIPEDIA_FILE =
  // "fake-wikipedia.xml";
  "d:\\wikipedia\\enwiki-20160204-pages-articles.xml";

[[maybe_unused]]
static constexpr const char *WIKIPEDIA_CCZ_FILE =
  "d:\\wikipedia\\enwiki-20160204_9.ccz";

using Article = Wikipedia::Article;

static constexpr int NUM_THREADS = 12;

static std::mutex count_mutex;
static std::unordered_map<uint32_t, int64_t> counts;
static int64_t total_count = 0;

static void Process() {
  #if 1
  std::unique_ptr<Wikipedia> wiki(
      Wikipedia::CreateFromCompressed(WIKIPEDIA_CCZ_FILE));
  CHECK(wiki.get() != nullptr) << WIKIPEDIA_CCZ_FILE;
  #else
  std::unique_ptr<Wikipedia> wiki(
      Wikipedia::Create(WIKIPEDIA_FILE));
  CHECK(wiki.get() != nullptr) << WIKIPEDIA_FILE;
  #endif

  Config config = Config::ParseConfig("fixedersys2x.cfg");
  FontImage font(config);
  CHECK(font.MappedCodepoint('*')) << "Couldn't load the font?";

  Timer timer;

  auto OneArticle = [](Article *article) {
      uint32_t low[127] = {0};
      unordered_map<uint32_t, uint32_t> rest;
      int64_t total = 0;

      auto OneString = [&low, &rest, &total](const std::string &s) {
          std::vector<uint32_t> codepoints = UTF8::Codepoints(s);
          total += codepoints.size();
          for (uint32_t c : codepoints) {
            if (c >= 0 && c < 128) {
              low[(uint32_t)c]++;
            } else {
              rest[(uint32_t)c]++;
            }
          }
      };

      OneString(article->title);
      OneString(article->body);

      {
        MutexLock ml(&count_mutex);
        total_count += total;
        for (int i = 0; i < 127; i++) {
          if (low[i] > 0) counts[i] += low[i];
        }
        for (const auto &[code, count] : rest) {
          counts[code] += count;
        }
      }
    };

  int64_t num_articles = 0;
  {
    Periodically status_per(5.0);
    Asynchronously async(NUM_THREADS);
    for (;;) {
      auto ao = wiki->Next();
      if (!ao.has_value()) break;

      async.Run([&OneArticle, article = std::move(ao.value())]() mutable {
          OneArticle(&article);
        });

      num_articles++;
      if (status_per.ShouldRun()) {
        const double total_sec = timer.MS() / 1000.0;
        {
          MutexLock ml(&count_mutex);
          Print("{}  [{:.2f}/sec] {} count\n", num_articles,
                 num_articles / total_sec, total_count);
        }
      }
    }

    Print("Wait for async to finish.\n");
  }

  Print("{} articles done in {:.3f} minutes.\n",
        num_articles, timer.Seconds() / 60.0);

  string out = std::format("Total codepoints: {}\n", total_count);
  std::vector<std::pair<uint32_t, int64_t>> sorted;
  sorted.reserve(counts.size());
  for (const auto &[code, count] : counts)
    sorted.emplace_back(code, count);
  Print("{} nonzero codepoints\n", sorted.size());

  // Maybe could group some CJK codepoints into pages?

  // Sort ascending.
  std::sort(sorted.begin(), sorted.end(),
            [](const std::pair<uint32_t, int64_t> &a,
               const std::pair<uint32_t, int64_t> &b) {
              return a.second > b.second;
            });
  for (const auto &[code, count] : sorted) {
    if (!font.MappedCodepoint(code)) {
      std::string utf8 = UTF8::Encode(code);
      AppendFormat(&out, "U+{:04x} ({}): {}\n", code, utf8, count);
    }
  }

  Util::WriteFile("unicode.txt", out);
  Print("Wrote unicode.txt\n");
  Print("Finished in {}\n", ANSI::Time(timer.Seconds()));
  wiki->PrintStats();
}

int main(int argc, char **argv) {
  ANSI::Init();

  Process();
  return 0;
}
