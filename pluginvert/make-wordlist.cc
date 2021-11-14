// Generate the list of most frequent N words in wikipedia.

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <chrono>
#include <thread>
#include <cstdio>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "threadutil.h"
#include "util.h"
#include "wikipedia.h"

using namespace std;

using int64 = int64_t;

static constexpr const char *WORDLIST = "wordlist.txt";

static constexpr int WORDLIST_SIZE = 65536;

// a-z, but also allow common words like "it's".
static bool OkChars(const string &s) {
  if (s.empty()) return false;
  if (s[0] == '\'') return false;
  for (int i = 0; i < s.size(); i++) {
    if (s[i] == '\'' ||
        (s[i] >= 'a' && s[i] <= 'z')) {
      // ok
    } else {
      return false;
    }
  }
  return true;
}

static void MakeWordlist() {
  static constexpr int NUM_SHARDS = 128;

  std::vector<string> filenames;
  for (int i = 0; i < NUM_SHARDS; i++)
    filenames.push_back(StringPrintf("wikibits/wiki-%d.txt", i));
  std::vector<std::unordered_map<string, int64>> processed =
    ParallelMap(filenames,
                [](const std::string &filename) {
                  printf("Reading %s...\n", filename.c_str());
                  std::vector<std::vector<string>> frags;
                  std::unordered_map<string, int64> counts;
                  int64 not_valid = 0;
                  auto co = Util::ReadFileOpt(filename);
                  CHECK(co.has_value());
                  string contents = std::move(co.value());
                  int64 pos = 0;
                  auto ReadByte = [&filename, &contents, &pos]() ->
                    uint8_t {
                      CHECK (pos < contents.size())
                        << filename << " @ " << pos << " of "
                        << contents.size();
                      return (uint8_t)contents[pos++];
                    };

                  auto Read32 = [&ReadByte]() {
                      uint32_t a = ReadByte();
                      uint32_t b = ReadByte();
                      uint32_t c = ReadByte();
                      uint32_t d = ReadByte();
                      return (a << 24) | (b << 16) | (c << 8) | d;
                    };

                  auto ReadString = [&contents, &pos, &ReadByte](int len) {
                      // printf("Read string of length %d:\n", len);
                      string s;
                      s.reserve(len);
                      for (int i = 0; i < len; i++) {
                        CHECK(pos < contents.size())
                          << i << "/" << len;
                        char c = ReadByte();
                        // printf("[%c]", c);
                        s.push_back(c);
                      }
                      return s;
                    };

                  int64 num_articles = 0;
                  auto Write = [&](uint32_t x) {
                    return StringPrintf("(%lld) %d [%c%c%c%c]",
                                        num_articles,
                                        x,
                                        (x >> 24) & 0xFF,
                                        (x >> 16) & 0xFF,
                                        (x >>  8) & 0xFF,
                                        x         & 0xFF);
                    };

                  while (pos < contents.size()) {
                    Wikipedia::Article art;
                    uint32_t t_len = Read32();
                    CHECK(t_len < 1000) << Write(t_len);
                    art.title = ReadString(t_len);
                    uint32_t b_len = Read32();
                    CHECK(b_len < 10000000) << Write(b_len);
                    art.body = ReadString(b_len);

                    for (int i = 0; i < art.title.size(); i++) {
                      CHECK(art.title[i] != 0);
                    }

                    for (int i = 0; i < art.body.size(); i++) {
                      CHECK(art.body[i] != 0);
                    }

                    // but convert article to words.
                    std::vector<string> tokens =
                      Util::Tokens(art.body,
                                   [](char c) { return isspace(c); });
                    // TODO: Other normalization here (remove outside
                    // double-quotes, maybe rewrite internal apostrophes?
                    for (string &s : tokens) {
                      s = Util::lcase(s);
                      // XXX TODO: This should actually end the fragment
                      if (s.back() == '.') s.pop_back();
                      else if (s.back() == ',') s.pop_back();
                      else if (s.back() == ';') s.pop_back();
                    }


                    for (int i = 0; i < tokens.size(); i++) {
                      const string &token = tokens[i];
                      if (OkChars(token)) {
                        counts[token]++;
                      } else {
                        not_valid++;
                      }
                    }
                    num_articles++;
                  }

                  printf("Distinct words: %lld, "
                         "Not valid: %lld\n",
                         counts.size(), not_valid);
                  return counts;
                }, 8);

  std::unordered_map<std::string, int64> counts;

  printf("Now build all map:\n");
  for (const auto &m : processed) {
    for (const auto &[s, c] : m) {
      counts[s] += c;
    }
  }

  printf("Distinct words: %lld\n", counts.size());

  std::vector<std::pair<std::string, int64>> all;
  for (const auto &[s, c] : counts) {
    all.emplace_back(s, c);
  }

  // Sort by descending frequency.
  std::sort(all.begin(), all.end(),
            [](const std::pair<string, int64> &a,
               const std::pair<string, int64> &b) {
              if (a.second == b.second) {
                return a.first < b.first;
              } else {
                return a.second > b.second;
              }
            });

  printf("Sorted.\n");

  CHECK(all.size() > WORDLIST_SIZE) << "Unexpectedly small number "
    "of distinct words??";
  all.resize(WORDLIST_SIZE);
  FILE *f = fopen(WORDLIST, "wb");
  CHECK(f != nullptr);
  for (const auto &[s, c] : all) {
    fprintf(f, "%s\n", s.c_str(), c);
  }
  fclose(f);
  printf("Wrote %s\n", WORDLIST);
}

int main(int argc, char **argv) {
  MakeWordlist();

  printf("OK\n");
  return 0;
}
