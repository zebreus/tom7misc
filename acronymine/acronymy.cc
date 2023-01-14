
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

#include "freq.h"
#include "word2vec.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

static constexpr const char *WIKIPEDIA_FILE =
  // "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

static constexpr const char *WORD2VEC_FILE =
  "c:\\code\\word2vec\\GoogleNews-vectors-negative300.bin";
static constexpr const char *WORD2VEC_FILL_FILE = "word2vecfill.txt";

using Article = Wikipedia::Article;

static constexpr int MIN_WORD = 5;
static constexpr int MAX_WORD = 24;
static constexpr int NUM_THREADS = 12;
static constexpr int MAX_ACRONYMS = 50;

static std::unordered_set<string> *words = nullptr;
static Freq *freq = nullptr;

static std::mutex acronym_mutex;
// Map strings to their expansions, each with its score.
static std::unordered_map<
  string, std::vector<std::pair<float, string>>> *acronyms = nullptr;
static std::map<int, int64> *num_acronyms = nullptr;

static void TakeBestAcronyms(std::vector<std::pair<float, string>> *v) {
  {
    // Keep at most one copy of each acronym.
    std::unordered_set<string> seen;
    std::vector<std::pair<float, string>> keep;
    keep.reserve(v->size());
    for (const auto &[f, s] : *v) {
      if (!seen.contains(s)) {
        keep.emplace_back(f, s);
        seen.insert(s);
      }
    }
    *v = std::move(keep);
  }

  std::sort(v->begin(), v->end(),
            [](const std::pair<float, std::string> &a,
               const std::pair<float, std::string> &b) {
              return b.first < a.first;
            });
  if (v->size() > MAX_ACRONYMS)
    v->resize(MAX_ACRONYMS);
}

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

  auto ScoreAcronym = [&w2v](
      const std::string &word,
      const std::vector<std::string> &expansion) ->
    float {
      const int wordidx = w2v->GetWord(word);
      if (wordidx < 0) return -.07f;

      // its "probability" based on frequency (but compared to
      // the most probable sentence, "the the the ...")
      double prob = 1.0;

      std::vector<float> expv(w2v->Size(), 0.0f);
      for (const std::string &w : expansion) {
        const int widx = w2v->GetWord(w);
        if (widx >= 0) {
          // XXX compare using raw vec here.
          std::vector<float> wv = w2v->NormVec(widx);
          for (int i = 0; i < expv.size(); i++)
            expv[i] += wv[i];
        }
        prob *= freq->NormalizedFreq(w);
      }
      Word2Vec::Norm(&expv);

      // Hack so that the probabilities are not so small.
      prob = sqrt(sqrt(prob));

      // Score is cosine similarity now.
      return 0.75 * w2v->Similarity(expv, wordidx) + 0.25 * prob;
    };

  auto OneArticle = [&Normalize, &ScoreAcronym, &wiki](Article *article) {
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

          // Only consider it if it's in the dictionary.
          if (!words->contains(word)) {
            lastn_words.push_back("*");
            lastn_chars.push_back('*');
          } else {
            lastn_words.push_back(word);
            lastn_chars.push_back(word[0]);

            // Now do we have any acronyms?
            for (int len = MIN_WORD; len <= MAX_WORD; len++) {
              string w;
              for (int i = 0; i < len; i++) {
                char c = lastn_chars[lastn_chars.size() - len + i];
                if (c == '*') goto next_article_word;
                w += c;
              }

              if (words->contains(w)) {
                std::vector<string> expansion;
                expansion.reserve(len);
                for (int i = 0; i < len; i++) {
                  expansion.push_back(
                      lastn_words[lastn_words.size() - len + i]);
                }

                const float score = ScoreAcronym(w, expansion);
                // Have a word.
                const string phrase = Util::Join(expansion, " ");

                {
                  MutexLock ml(&acronym_mutex);
                  vector<pair<float, string>> &v = (*acronyms)[w];
                  (*num_acronyms)[w.size()]++;
                  v.push_back(std::make_pair(score, phrase));
                  // PERF: Use TopN.
                  if (v.size() > MAX_ACRONYMS * 2) {
                    TakeBestAcronyms(&v);
                  }
                }
              }
            }
          }
        }
      next_article_word:;
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

  // Since we amortize sorting, we need to trim the vectors
  // down to the actual budget.
  for (auto &[w, v] : *acronyms) {
    TakeBestAcronyms(&v);
  }

  {
    std::vector<std::tuple<float, string, string>> all;
    for (const auto &[word, v] : *acronyms) {
      for (const auto &[score, ac] : v) {
        // invalidate them if they contain the word itself
        if (ac.find(word) == string::npos) {
          all.emplace_back(score, word, ac);
        }
      }
    }
    std::sort(all.begin(), all.end(),
              [](const auto &a,
                 const auto &b) {
                return std::get<0>(a) > std::get<0>(b);
              });
    if (all.size() > 10000) all.resize(10000);
    string out;
    for (const auto &[score, w, ac] : all) {
      StringAppendF(&out, "%.3f %s: %s\n", score, w.c_str(), ac.c_str());
    }
    Util::WriteFile("acronyms-best.txt", out);
  }

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
    for (const auto &[score, ac] : v) {
      fprintf(f, "  %.3f. %s\n", score, ac.c_str());
    }

    if (word.size() >= 9) {
      StringAppendF(&longones, "%s:\n", word.c_str());
      for (const auto &[score, ac] : v) {
        StringAppendF(&longones, "  %3f. %s\n", score, ac.c_str());
      }
    }
  }

  for (FILE *f : outf) fclose(f);
  printf("Wrote " AGREEN("acronyms-?.txt") "\n");

  Util::WriteFile("acronyms-long.txt", longones);
}

int main(int argc, char **argv) {
  AnsiInit();

  acronyms = new std::unordered_map<string, vector<pair<float, string>>>;
  num_acronyms = new std::map<int, int64>;

  int max_word = 0;
  vector<string> wordlist =
    Util::ReadFileToLines("word-list.txt");
  words = new unordered_set<string>;
  for (string &s : wordlist) {
    max_word = std::max(max_word, (int)s.size());
    words->insert(std::move(s));
  }
  printf("Longest word: %d\n", max_word);

  freq = Freq::Load("freq.txt", *words);
  CHECK(freq != nullptr);

  Process();
  return 0;
}
