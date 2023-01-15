
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
  CHECK(most.size() >= 500);
  for (int i = 0; i < 500; i++) {
    uint32_t props = wordnet->GetProps(most[i]);
    printf("  %08x %s %s\n", props, most[i].c_str(),
           WordNet::PropString(props).c_str());
  }
}


int main(int argc, char **argv) {
  TestFreq();

  printf("OK\n");
  return 0;
}
