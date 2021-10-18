
#include "wikipedia.h"

#include <string>
#include <optional>
#include <memory>
#include <stdio.h>

#include "base/logging.h"
#include "util.h"

static constexpr const char *FILENAME =
  "fake-wikipedia.xml";
  //  "d:\\rivercity\\wikipedia\\enwiki-20160204-pages-articles.xml";

using Article = Wikipedia::Article;

static void Dump() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create(FILENAME));
  CHECK(wiki.get() != nullptr) << FILENAME;

  int64_t num_articles = 0;
  int64_t num_redirects = 0;
  for (;;) {
    auto ao = wiki->Next();
    if (!ao.has_value()) break;
    const Article &article = ao.value();
    // printf("Title [%s]\n", article.title.c_str());
    num_articles++;
    if (wiki->IsRedirect(article)) {
      // printf("(redirect)\n");
      num_redirects++;
    } else {
      // printf("Body [%s]\n", article.body.c_str());
    }
    if (num_articles % 10000 == 0) printf("%lld\n", num_articles);
  }

  printf("%lld articles. %lld are redirects\n",
         num_articles, num_redirects);
}

int main(int argc, char **argv) {
  Dump();
  return 0;
}
