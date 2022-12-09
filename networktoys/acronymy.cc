
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

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

static constexpr const char *FILENAME =
  // "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

using Article = Wikipedia::Article;

static constexpr int MIN_WORD = 5;
static constexpr int MAX_WORD = 24;
static constexpr int NUM_THREADS = 12;
static constexpr int MAX_ACRONYMS = 50;

static std::unordered_set<string> *words = nullptr;

static std::mutex acronym_mutex;
static std::unordered_map<string, std::vector<string>> *acronyms = nullptr;
static std::map<int, int64> *num_acronyms = nullptr;

static void Process() {

  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create(FILENAME));
  CHECK(wiki.get() != nullptr) << FILENAME;

  auto Normalize = [&wiki](Article *art) {
      art->body = wiki->ReplaceEntities(std::move(art->body));
      art->body = wiki->RemoveTags(art->body);
      wiki->RemoveWikilinks(&art->body);
      wiki->RemoveMarkup(&art->body);
      wiki->ASCIIify(&art->body);
    };

  Timer timer;

  auto OneArticle = [&Normalize, &wiki](Article *article) {
      // printf("Article " ACYAN("%s") "\n", article->title.c_str());
      Normalize(article);

      if (wiki->IsRedirect(*article))
        return;

      // Rolling buffer of the last n words seen.
      LastNBuffer<string> lastn_words(MAX_WORD, "*");
      LastNBuffer<char> lastn_chars(MAX_WORD, '*');

      std::vector<string> article_words =
        Util::Tokens(article->body,
                     [](char c) {
                       // Break on anything not a-z0-9.
                       return !std::isalnum(c);
                     });

      for (string &word : article_words) {
        word = Util::lcase(word);
        if (!word.empty()) {
          lastn_words.push_back(word);
          lastn_chars.push_back(word[0]);

          // Any words in dict?
          for (int len = MIN_WORD; len <= MAX_WORD; len++) {
            string w;
            for (int i = 0; i < len; i++) {
              w += lastn_chars[lastn_chars.size() - len + i];
            }

            auto it = words->find(w);
            if (it != words->end()) {
              // Have a word.
              string phrase;
              for (int i = 0; i < len; i++) {
                if (i != 0) phrase += ' ';
                phrase += lastn_words[lastn_words.size() - len + i];
              }

              {
                MutexLock ml(&acronym_mutex);
                vector<string> &v = (*acronyms)[w];
                (*num_acronyms)[w.size()]++;
                if (v.size() < MAX_ACRONYMS) {
                  v.push_back(std::move(phrase));
                }
              }
            }

          }
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
        double total_sec = timer.MS() / 1000.0;
        printf("%lld  [%.2f/sec]\n", num_articles,
               num_articles / total_sec);
        {
          MutexLock ml(&acronym_mutex);
          for (int len = MIN_WORD; len <= MAX_WORD; len++) {
            int64 num = (*num_acronyms)[len];
            printf("  len " ABLUE("%d") ": %s%lld" ANSI_RESET "\n",
                   len,
                   num == 0 ? ANSI_GREY : ANSI_YELLOW,
                   num);
          }
        }
      }
    }

    printf("Wait for async to finish.\n");
  }

  printf("%lld articles done in %.3f minutes.\n",
         num_articles, timer.Seconds() / 60.0);

  string longones;

  printf("Writing files...\n");
  std::vector<FILE *> outf;
  for (char c = 'a'; c <= 'z'; c++) {
    const string filename = StringPrintf("acronyms-%c.txt", c);
    FILE *f = fopen(filename.c_str(), "wb");
    CHECK(f != nullptr) << filename;
    outf.push_back(f);
  }
  CHECK(outf.size() == 26);
  for (const auto &[word, v] : *acronyms) {
    CHECK(!word.empty());
    char c = word[0];
    CHECK(c >= 'a' && c <= 'z') << word;
    FILE *f = outf[c - 'a'];
    CHECK(f != nullptr);
    fprintf(f, "%s:\n", word.c_str());
    for (const string &ac : v) {
      fprintf(f, "  %s\n", ac.c_str());
    }

    if (word.size() >= 9) {
      StringAppendF(&longones, "%s:\n", word.c_str());
      for (const string &ac : v) {
        StringAppendF(&longones, "  %s\n", ac.c_str());
      }
    }
  }

  for (FILE *f : outf) fclose(f);
  printf("Wrote " AGREEN("acronyms-?.txt") "\n");

  Util::WriteFile("acronyms-long.txt", longones);
}

int main(int argc, char **argv) {
  AnsiInit();

  acronyms = new std::unordered_map<string, vector<string>>;
  num_acronyms = new std::map<int, int64>;
  {
    int max_word = 0;
    vector<string> wordlist =
      Util::ReadFileToLines("..\\manarags\\wordlist.asc");
    words = new unordered_set<string>;
    for (string &s : wordlist) {
      max_word = std::max(max_word, (int)s.size());
      if (s.size() >= MIN_WORD) {
        words->insert(std::move(s));
      }
    }
    printf("Longest word: %d\n", max_word);
  }

  Process();
  return 0;
}
