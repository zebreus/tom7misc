
#include "wikipedia.h"

#include <codecvt>
#include <locale>
#include <string>
#include <optional>
#include <memory>
#include <stdio.h>
#include <unordered_map>
#include <cctype>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "base/logging.h"
#include "util.h"
#include "re2/re2.h"
#include "timer.h"
#include "city/city.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "threadutil.h"
#include "periodically.h"
#include "wikipedia.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

static constexpr const char *WIKIPEDIA_FILE =
  // "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

using Article = Wikipedia::Article;

static constexpr int NUM_THREADS = 12;

static std::mutex count_mutex;
static std::unordered_map<uint32_t, int64_t> counts;
static int64 total_count = 0;

// ???? https://en.cppreference.com/w/cpp/locale/codecvt
template<class Facet>
struct deletable_facet : Facet
{
    template<class... Args>
    deletable_facet(Args&&... args) : Facet(std::forward<Args>(args)...) {}
    ~deletable_facet() {}
};

static void Process() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create(WIKIPEDIA_FILE));
  CHECK(wiki.get() != nullptr) << WIKIPEDIA_FILE;

  Timer timer;

  auto OneArticle = [&wiki](Article *article) {
      uint32_t low[127] = {0};
      unordered_map<uint32_t, uint32_t> rest;
      int64_t total = 0;

      auto OneString = [&low, &rest, &total](const std::string &s) {
          std::wstring_convert<
            deletable_facet<std::codecvt<char32_t, char, std::mbstate_t>>, char32_t> conv32;
          std::u32string str32 = conv32.from_bytes(s);

          /*
          std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv32;
          std::u32string str32 = conv32.from_bytes(s);
          */
          total += str32.size();
          for (char32_t c : str32) {
            if (c >= 0 && c < 128) low[(uint32_t)c]++;
            else rest[(uint32_t)c]++;
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
          printf("%lld  [%.2f/sec] %lld count\n", num_articles,
                 num_articles / total_sec, total_count);
        }
      }
    }

    printf("Wait for async to finish.\n");
  }

  printf("%lld articles done in %.3f minutes.\n",
         num_articles, timer.Seconds() / 60.0);

  string out = StringPrintf("Total codepoints: %lld\n", total_count);
  std::vector<std::pair<uint32_t, int64_t>> sorted;
  sorted.reserve(counts.size());
  for (const auto &[code, count] : counts)
    sorted.emplace_back(code, count);
  printf("%d nonzero codepoints\n", (int)sorted.size());

  std::sort(sorted.begin(), sorted.end(),
            [](const std::pair<uint32_t, int64_t> &a,
               const std::pair<uint32_t, int64_t> &b) {
              return b.second > a.second;
            });
  for (const auto &[code, count] : sorted) {
    std::string utf8 = Util::EncodeUTF8(code);
    StringAppendF(&out, "U+%x (%s): %lld\n", code, utf8.c_str(), count);
  }

  Util::WriteFile("unicode.txt", out);
  printf("Wrote unicode.txt\n");
}

int main(int argc, char **argv) {
  AnsiInit();

  Process();
  return 0;
}
