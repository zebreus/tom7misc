
#include "wikipedia.h"

#include <string>
#include <optional>
#include <memory>
#include <stdio.h>
#include <unordered_map>

#include "base/logging.h"
#include "util.h"
#include "re2/re2.h"
#include "timer.h"
#include "city/city.h"
#include "base/stringprintf.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

static constexpr const char *FILENAME =
  //  "fake-wikipedia.xml";
  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

using Article = Wikipedia::Article;

static constexpr int NUM_SHARDS = 128;

static void Dump() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create(FILENAME));
  CHECK(wiki.get() != nullptr) << FILENAME;

  std::vector<FILE *> outfiles;
  for (int i = 0; i < NUM_SHARDS; i++) {
    outfiles.push_back(fopen(StringPrintf("wikibits/wiki-%d.txt", i).c_str(),
                             "wb"));
    CHECK(outfiles.back() != nullptr);
  }

  auto Normalize = [&wiki](Article *art) {
      art->body = wiki->ReplaceEntities(std::move(art->body));
      art->body = wiki->RemoveTags(art->body);
      wiki->RemoveWikilinks(&art->body);
      wiki->RemoveMarkup(&art->body);
      wiki->ASCIIify(&art->body);
    };

  RE2 entity_re("(&[#A-Za-z0-9]*;)");
  std::unordered_map<string, int> entities;
  [[maybe_unused]]
  auto CountEntities = [&](const string &body) {
      StringPiece input(body);
      string ent;
      while (RE2::FindAndConsume(&input, entity_re, &ent))
        entities[ent]++;
    };

  Timer timer;

  int64_t num_articles = 0;
  int64_t num_redirects = 0;
  for (;;) {
    auto ao = wiki->Next();
    if (!ao.has_value()) break;
    Article article = std::move(ao.value());
    Normalize(&article);
    // CountEntities(article.body);
    #if 0
    if (entities.find("&ndash;") != entities.end()) {
      printf("??:\n%s\n", article.body.c_str());
      return;
    }
    #endif
    // printf("Title [%s]\n", article.title.c_str());
    num_articles++;
    if (wiki->IsRedirect(article)) {
      // printf("(redirect)\n");
      num_redirects++;
    } else {
      const uint64 h = CityHash64(article.title.data(),
                                  article.title.size());
      const uint64 shard = h % NUM_SHARDS;
      auto WriteByte = [](FILE *f, uint8 b) {
          CHECK(1 == fwrite(&b, 1, 1, f));
        };
      auto WriteString = [&WriteByte](FILE *f, const std::string &s) {
          uint32 size = s.size();
          WriteByte(f, (size >> 24) & 0xFF);
          WriteByte(f, (size >> 16) & 0xFF);
          WriteByte(f, (size >>  8) & 0xFF);
          WriteByte(f,  size        & 0xFF);
          if (size > 0) {
            CHECK(size == fwrite(s.data(), 1, size, f));
          }
        };
      CHECK(shard < outfiles.size());
      CHECK(outfiles[shard] != nullptr);
      WriteString(outfiles[shard], article.title);
      WriteString(outfiles[shard], article.body);
    }

    if (num_articles % 10000 == 0) {
      double total_sec = timer.MS() / 1000.0;
      printf("%lld  [%.2f/sec]\n", num_articles,
             num_articles / total_sec);
#if 0
      for (const auto &[ent, count] : entities) {
        printf("%s: %d\n", ent.c_str(), count);
      }
#endif
    }
  }

  printf("%lld articles. %lld are redirects\n",
         num_articles, num_redirects);

  for (FILE *f : outfiles) fclose(f);
}

int main(int argc, char **argv) {
  Dump();
  return 0;
}
