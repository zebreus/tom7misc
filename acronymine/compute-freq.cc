
// Get the frequency (count) of each word in wikipedia.

#include "wikipedia.h"

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
#include "lastn-buffer.h"

#include "word2vec.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int64 = int64_t;


static constexpr const char *WIKIPEDIA_FILE =
  // "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

static constexpr const char *WORD2VEC_FILE =
  "c:\\code\\word2vec\\GoogleNews-vectors-negative300.bin";
static constexpr const char *WORD2VEC_FILL_FILE = "word2vecfill.txt";

using Article = Wikipedia::Article;

static constexpr int NUM_THREADS = 12;
static constexpr int MAX_ACRONYMS = 50;

static std::unordered_set<string> *words = nullptr;

static std::mutex counts_m;
static std::unordered_map<string, int64> *counts = nullptr;

static void Process() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create(WIKIPEDIA_FILE));
  CHECK(wiki.get() != nullptr) << WIKIPEDIA_FILE;

  printf("%d words in dictionary\n", (int)words->size());
  std::unique_ptr<Word2Vec> w2v(
      Word2Vec::Load(WORD2VEC_FILE, WORD2VEC_FILL_FILE, *words));
  CHECK(w2v.get() != nullptr) << WORD2VEC_FILE;
  printf("Word2Vec: %d words (vec %d floats)\n",
         (int)w2v->NumWords(),
         w2v->Size());

  auto Normalize = [&wiki](Article *art) {
      art->body = wiki->ReplaceEntities(std::move(art->body));
      art->body = wiki->RemoveTags(art->body);
      wiki->RemoveWikilinks(&art->body);
      wiki->RemoveMarkup(&art->body);
      wiki->ASCIIify(&art->body);
    };

  Timer timer;

  auto OneArticle = [&Normalize, &wiki](Article *article) {
      Normalize(article);

      if (wiki->IsRedirect(*article))
        return;

      std::vector<string> article_words =
        Util::Tokens(article->body,
                     [](char c) {
                       // Break on anything not a-z0-9.
                       return !std::isalnum(c);
                     });

      std::unordered_map<string, int64> local_count;

      for (string &word : article_words) {
        word = Util::lcase(word);
        if (words->contains(word)) {
          local_count[word]++;
        }
      }

      {
        MutexLock ml(&counts_m);
        for (const auto &[w, c] : local_count)
          (*counts)[w] += c;
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
        double total_sec = timer.MS() / 1000.0;
        printf("%lld  [%.2f/sec]\n", num_articles,
               num_articles / total_sec);
      }
    }

    printf("Wait for async to finish.\n");
  }

  printf("%lld articles done in %.3f minutes.\n",
         num_articles, timer.Seconds() / 60.0);

  std::vector<std::pair<string, int64>> freqs;
  for (const auto &[w, c] : *counts) {
    freqs.emplace_back(w, c);
  }
  std::sort(freqs.begin(), freqs.end(),
            [](const std::pair<std::string, int64> &a,
               const std::pair<std::string, int64> &b) {
              return a.second > b.second;
            });

  string out;
  for (const auto &[w, c] : freqs) {
    StringAppendF(&out, "%s %lld\n", w.c_str(), c);
  }
  Util::WriteFile("freq.txt", out);

  printf("Wrote freq.txt. Most common missing words:\n");
  int64 missing = 0;
  for (const auto &[w, c] : freqs) {
    int idx = w2v->GetWord(w);
    if (idx < 0) {
      missing++;
      if (missing < 100) {
        printf("%s (%lld) mising from w2v\n",
               w.c_str(), c);
      }
    }
  }

  printf("\nDone.\n");
}

int main(int argc, char **argv) {
  AnsiInit();

  counts = new std::unordered_map<string, int64>;
  {
    int max_word = 0;
    vector<string> wordlist =
      Util::ReadFileToLines("word-list.txt");
    words = new unordered_set<string>;
    for (string &s : wordlist) {
      max_word = std::max(max_word, (int)s.size());
      words->insert(std::move(s));
    }
    printf("Longest word: %d\n", max_word);
  }

  Process();
  return 0;
}
