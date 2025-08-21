
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "citation-util.h"
#include "nice.h"
#include "re2/re2.h"
#include "threadutil.h"

using namespace std;
using int64 = int64_t;

int main(int argc, char **argv) {
  Nice::SetLowPriority();

  string outfile;

  vector<string> filenames;
  for (int i = 1; i < argc; i++) {
    string arg = (string)argv[i];
    if (arg == "-o") {
      CHECK(i < argc - 1);
      outfile = argv[i + 1];
      i++;
    } else {
      filenames.push_back(arg);
    }
  }

  if (filenames.empty()) {
    fprintf(stderr, "Pass JSON paper files on the command line.\n");
    return -1;
  }

  if (outfile.empty()) {
    fprintf(stderr, "Supply an output filename with -o.\n");
    return -1;
  }

  std::unordered_map<string, CiteStats> stat_map;
  RE2 line_re{"([^\t]*)\t([\\d]+)\t([\\d]+)"};
  auto OneFile = [&stat_map, &line_re](const string &filename) {
    printf("%s\n", filename.c_str());
    LocalForEachLine(filename, [&stat_map, &line_re](string line) {
      string author;
      int64 articles = 0LL, citations = 0LL;
      CHECK(RE2::FullMatch(line, line_re, &author, &articles, &citations))
          << line;

      CiteStats &stats = stat_map[author];
      stats.articles += articles;
      stats.citations += citations;
    });
  };

  UnParallelApp(filenames, OneFile, 8);

  printf("Writing %zu merged records to %s...\n", stat_map.size(),
         outfile.c_str());
  FILE *out = fopen(outfile.c_str(), "wb");
  CHECK(out != nullptr) << outfile.c_str();
  for (const auto &row : stat_map) {
    fprintf(out, "%s\t%lld\t%lld\n", row.first.c_str(), row.second.articles,
            row.second.citations);
  }
  fclose(out);

  return 0;
}
