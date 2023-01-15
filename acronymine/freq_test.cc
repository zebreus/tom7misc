
#include "freq.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

#include "util.h"

#include "base/logging.h"

using namespace std;

static void TestFreq() {
  unordered_set<string> words;
  for (string &s : Util::ReadFileToLines("word-list.txt"))
    words.insert(std::move(s));

  std::unique_ptr<Freq> freq(Freq::Load("freq.txt", words));
  CHECK(freq.get() != nullptr);

  double prob_the = freq->Probability("the");
  double prob_idealism = freq->Probability("idealism");
  printf("the:      %.6f\n"
         "idealism: %.6f\n",
         prob_the,
         prob_idealism);
  CHECK(prob_the > 0.0);
  CHECK(prob_idealism > 0.0);
  CHECK(prob_the > prob_idealism);

  double norm_the = freq->NormalizedFreq("the");
  CHECK(norm_the == 1.0);

  printf("Most frequent words:\n");
  std::vector<std::string> most = freq->SortedWords();
  CHECK(most.size() >= 10);
  for (int i = 0; i < 10; i++)
    printf("  %s\n", most[i].c_str());
}

int main(int argc, char **argv) {
  TestFreq();
  printf("OK\n");
  return 0;
}
