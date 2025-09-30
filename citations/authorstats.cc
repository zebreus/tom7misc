#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <utility>
#include <vector>
#include <string>
#include <unordered_map>

#include "base/logging.h"
#include "base/print.h"
#include "citation-util.h"
#include "nice.h"
#include "re2/re2.h"
#include "textsvg.h"
#include "util.h"

using namespace std;
using int64 = int64_t;

int main(int argc, char **argv) {
  Nice::SetLowPriority();

  CHECK_EQ(argc, 3) << "authorstats.exe infile.txt outfile.svg\n";
  string infile = argv[1];
  string outfile = argv[2];

  // Number of articles authored by each name.
  std::unordered_map<string, CiteStats> cite_stats;
  int64 authors_bad = 0LL, articles_bad = 0LL;
  int64 articles_kept = 0LL, citations_kept = 0LL;
  RE2 line_re{"([^\t]*)\t([\\d]+)\t([\\d]+)"};
  LocalForEachLine(infile,
       [&cite_stats, &line_re, &authors_bad, &articles_bad,
        &articles_kept, &citations_kept](string line) {
    string author;
    int64 articles = 0LL, citations = 0LL;
    CHECK(RE2::FullMatch(line, line_re, &author, &articles, &citations)) <<
      line;

    string dict;
    if (Dictionaryize<true>(author, &dict)) {
      CiteStats &stats = cite_stats[dict];
      stats.articles += articles;
      stats.citations += citations;
      articles_kept += articles;
      citations_kept += citations;
    } else {
      authors_bad++;
      articles_bad += articles;
    }
  });

  Print("Got stats for {} keys, "
         "with {} rejected ({} articles rejected)\n"
         "Total kept articles: {}  and citations: {}\n",
         (int64)cite_stats.size(), authors_bad, articles_bad, articles_kept,
         citations_kept);

  vector<std::pair<string, CiteStats>> rows;
  rows.reserve(cite_stats.size());
  for (const auto &p : cite_stats) {
    rows.emplace_back(p.first, p.second);
  }

  std::sort(rows.begin(), rows.end(),
      [](const std::pair<string, CiteStats> &a,
         const std::pair<string, CiteStats> &b) {
        return a.first < b.first;
      });

  Print("Sorted.\n");

  // Now write CDFs.
  {
    constexpr double XSCALE = 1024.0, YSCALE = 1024.0;
    string articles_cdf =
        "<polyline stroke-linejoin=\"round\" "
        "fill=\"none\" stroke=\"#800\" stroke-opacity=\"0.75\" "
        "stroke-width=\"1.5\" points=\"";
    articles_cdf.reserve(1 << 23);
    string citations_cdf =
        "<polyline stroke-linejoin=\"round\" "
        "fill=\"none\" stroke=\"#008\" stroke-opacity=\"0.75\" "
        "stroke-width=\"1.5\" points=\"";
    citations_cdf.reserve(1 << 23);

    string text;
    text.reserve(1 << 21);

    int64 articles = 0LL, citations = 0LL;
    bool ahead = false;
    for (int i = 0; i < rows.size(); i++) {
      articles += rows[i].second.articles;
      citations += rows[i].second.citations;
      if (i % 10000 == 0) {
        // Rank of author
        double x = i / (double)rows.size();
        double ay = 1.0 - (articles / (double)articles_kept);
        double cy = 1.0 - (citations / (double)citations_kept);
        articles_cdf += std::format("{},{} ", Rtos(x * XSCALE),
                                     Rtos(ay * YSCALE).c_str());
        citations_cdf += std::format("{},{} ", Rtos(x * XSCALE),
                                     Rtos(cy * YSCALE));

        if (i % 1000000 == 0) {
          text += TextSVG::Text(x * XSCALE, 0.9 * YSCALE, "sans-serif", 12.0,
                                {{"#000", rows[i].first}});
          text += "\n";
        }

        if (cy >= ay && !ahead) {
          Print("Crossover to cy >= ay ({:.4f} <= {:.4f}) at {}. {}\n", cy, ay, i,
                 rows[i].first);
          ahead = true;
        } else if (cy <= ay && ahead) {
          Print("Crossover to cy <= ay ({:.4f} <= {:.4f}) at {}. {}\n", cy, ay, i,
                 rows[i].first);
          ahead = false;
        }
      }
    }

    string svg = TextSVG::Header(XSCALE, YSCALE);
    svg += articles_cdf + "\" />\n";
    svg += citations_cdf + "\" />\n";
    svg += text;
    svg += TextSVG::Footer();
    Util::WriteFile(outfile, svg);
    Print("Wrote {}\n", outfile);
  }

  {
    string sorted_outfile = infile + "sorted.txt";
    Print("Writing {} sorted records to {}...\n", rows.size(),
          sorted_outfile);
    FILE *out = fopen(sorted_outfile.c_str(), "wb");
    CHECK(out != nullptr) << outfile.c_str();
    for (const auto &row : rows) {
      Print(out, "{}\t{}\t{}\n", row.first, row.second.articles,
            row.second.citations);
    }
    fclose(out);
  }

  return 0;
}
