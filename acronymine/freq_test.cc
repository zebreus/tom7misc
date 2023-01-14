
#include "freq.h"

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

#include "util.h"

#include "base/logging.h"

using namespace std;

static void TestFreq() {
  unordered_set<string> words;
  for (string &s : Util::ReadFileToLines("word-list.txt"))
    words.insert(std::move(s));

  Freq *freq = Freq::Load("freq.txt", words);
  CHECK(freq != nullptr);

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
}


int main(int argc, char **argv) {
  TestFreq();
  printf("OK\n");
  return 0;
}
