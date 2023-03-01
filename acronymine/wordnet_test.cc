
#include "wordnet.h"

#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

#include "freq.h"
#include "base/logging.h"
#include "util.h"

using namespace std;

static void TestFreq() {
  unordered_set<string> words;
  for (string &s : Util::ReadFileToLines("word-list.txt"))
    words.insert(std::move(s));

  std::unique_ptr<WordNet> wordnet(
      WordNet::Load("wordnet", words));

  std::unique_ptr<Freq> freq(Freq::Load("freq.txt", words));
  CHECK(freq.get() != nullptr);

  printf("Most frequent words:\n");
  std::vector<std::string> most = freq->SortedWords();
  // CHECK(most.size() >= 1000);
  int64_t missing = 0, present = 0;
  for (int i = 0; i < most.size(); i++) {
    const string &word = most[i];
    uint32_t props = wordnet->GetProps(word);
    int64_t f = freq->RawFreq(word);
    printf("  %09lld %08x %s %s\n", f, props, word.c_str(),
           WordNet::PropString(props).c_str());

    if (f > 0) {
      if (props == 0) {
        missing += (f - 1);
      } else {
        present += (f - 1);
      }
    }
  }

  double denom = (missing + present) / 100.0;
  fprintf(stderr, "%lld occ. missing, %lld present  (%.3f%% / %.3f%%)\n",
          missing, present, missing / denom, present / denom);
}


int main(int argc, char **argv) {
  TestFreq();

  printf("OK\n");
  return 0;
}
