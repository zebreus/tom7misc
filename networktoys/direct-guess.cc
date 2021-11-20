
#include "direct-word-problem.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>

#include "network.h"
#include "base/logging.h"

using namespace std;

static void Guess(const std::vector<string> &input_words) {
  Wordlist wordlist;
  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  vector<int> ids;
  for (const string &word : input_words) {
    int id = wordlist.GetId(word);
    CHECK(id >= 0) << "Not in the wordlist: " << word;
    ids.push_back(id);
  }

  Stimulation stim{*net};

  vector<float> input = DirectWordProblem::LoadInput(ids);
  CHECK(input.size() == stim.values.front().size());
  stim.values.front() = std::move(input);

  net->RunForward(&stim);

  vector<int> outids = DirectWordProblem::DecodeOutput(
      stim.values.back());
  CHECK(outids.size() == NUM_WORDS);

  // Guessed word is at the end.
  auto Print = [&](int i, bool paren) {
      if (i != 0) printf(" ");
      if (paren) printf("(");
      if (outids[i] == -1)
        printf("-");
      else
        printf("%s", wordlist.GetWord(outids[i]).c_str());
      if (paren) printf(")");
    };
  for (int i = 0; i < WORDS_BEFORE; i++) Print(i, false);
  Print(WORDS_BEFORE + WORDS_AFTER, true);
  for (int i = 0; i < WORDS_AFTER; i++) Print(WORDS_BEFORE + i, false);

  printf("\nOK\n");
}

int main(int argc, char **argv) {
  std::vector<string> words;
  if (argc != 1 + WORDS_BEFORE + WORDS_AFTER) {
    printf("./direct-guess.exe word1 word2 word3 ... wordn\n"
           "(Where the guess pattern is  %d words ___ %d words.)\n",
           WORDS_BEFORE, WORDS_AFTER);
    return -1;
  }
  for (int i = 1; i < argc; i++)
    words.emplace_back(argv[i]);
  Guess(words);
  return 0;
}
