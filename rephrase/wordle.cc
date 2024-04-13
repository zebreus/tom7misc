
#include <string>
#include <vector>

#include "util.h"
#include "base/logging.h"
#include "ansi.h"

enum Color { GREY, YELLOW, GREEN, };

static std::vector<Color>
Color1(const std::string &secret, const std::string &guess) {
  CHECK(secret.size() == 5);
  CHECK(guess.size() == 5);

  std::map<char, int> counts;
  for (char c : secret) counts[c]++;

  std::vector<Color> colors(5, GREY);
  for (int i = 0; i < 5; i++) {
    char c = guess[i];
    if (secret[i] == c) {
      colors[i] = GREEN;
      counts[c]--;
    }
  }

  for (int i = 0; i < 5; i++) {
    if (colors[i] == GREY) {
      char c = guess[i];
      if (counts[c] > 0) {
        colors[i] = YELLOW;
        counts[c]--;
      } else {
        // stay grey
      }
    }
  }

  return colors;
}

void PrintColor(const std::vector<Color> &colors,
                const std::string &guess) {
  CHECK(guess.size() == colors.size());
  for (int i = 0; i < (int)colors.size(); i++) {
    const char *a = "";
    switch (colors[i]) {
    case GREY:
      a = ANSI_BG(50, 50, 50);
      break;
    case YELLOW:
      a = ANSI_BG(100, 100, 0);
      break;
    case GREEN:
      a = ANSI_BG(0, 164, 0);
      break;
    }

    printf("%s %c " ANSI_RESET, a, guess[i] & ~32);
  }
}

void Print(const std::string &secret, const std::string &guess) {
  auto colors = Color1(secret, guess);
  PrintColor(colors, guess);
  printf("\n");
}

std::vector<std::string> Filter(
    const std::vector<std::string> &words,
    const std::vector<std::pair<std::string, std::vector<Color>>> &board) {
  std::vector<std::string> ret;
  for (const std::string &word : words) {
    for (const auto &[guess, result] : board) {
      if (Color1(word, guess) != result) {
        goto exclude;
      }
    }
    ret.push_back(word);
  exclude:;
  }
  return ret;
}

static void PrintBoard(
    const std::vector<std::pair<std::string, std::vector<Color>>> &board) {
  for (const auto &[guess, colors] : board) {
    PrintColor(colors, guess);
    printf("\n");
  }
}

int main(int argc, char **argv) {

  std::vector<std::string> words = Util::ReadFileToLines("wordle.txt");

  {
    std::vector<std::pair<std::string, std::vector<Color>>>
      board = {
      {"wrong", {GREY,  GREY,   GREY,   GREY, GREY}},
      {"rules", {GREY,  GREY,   GREY,   YELLOW, YELLOW}},
      {"times", {GREY,  GREY,   YELLOW, YELLOW, YELLOW}},
      {"shame", {GREY,  GREY,   GREY,   GREEN, GREEN}},
    };

    std::vector<std::string> remain =
      Filter(words, board);

    if (remain.empty()) {
      printf("No words.\n");
    }
    for (const auto &s : remain) {
      printf("%s\n", s.c_str());
    }
  }

  words.push_back("aeoud");
  words.push_back("bovex");
  words.push_back("knuth");

  std::string secret = "messy";
  std::vector<std::pair<std::string, std::vector<Color>>>
    board;
  for (const std::string s : {
      // "fight",
      // "trash",
      // "times", "wrong", "rules",
      "wrong", "rules", "times",
      "shame",
      // "shame",
    }) {
    board.emplace_back(s, Color1(secret, s));
  }

  PrintBoard(board);

  /*
  Print("sorry", "fight");
  Print("sorry", "times");
  Print("sorry", "wrong");
  Print("sorry", "rules");
  */

  std::vector<std::string> remain =
    Filter(words, board);

  for (const auto &s : remain) {
    printf("%s\n", s.c_str());
  }

  return 0;
}
