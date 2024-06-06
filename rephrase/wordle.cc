
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "util.h"
#include "base/logging.h"
#include "ansi.h"

enum Color { GREY, YELLOW, GREEN, };

static std::vector<Color>
Color1(const std::string &secret, const std::string &guess) {
  CHECK(secret.size() == 5) << secret;
  CHECK(guess.size() == 5) << guess;

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

static void ShowPuzzle(const std::vector<std::string> &words,
                       const std::string &secret,
                       const std::vector<std::string> &guesses) {

  std::vector<std::pair<std::string, std::vector<Color>>>
    board;
  for (const std::string &s : guesses) {
    board.emplace_back(s, Color1(secret, s));
  }

  PrintBoard(board);

  std::vector<std::string> remain =
    Filter(words, board);

  for (const auto &s : remain) {
    printf("%s\n", s.c_str());
  }
}

int main(int argc, char **argv) {

  std::vector<std::string> words = Util::ReadFileToLines("wordle.txt");

  words.push_back("bovex");
  words.push_back("knuth");

  {
    std::vector<std::pair<std::string, std::vector<Color>>>
      board = {
      {"wrong", {GREY,   GREY,   GREY,   GREY,  GREY}},
      {"rules", {GREY,   GREY,   GREY,   YELLOW, YELLOW}},
      {"times", {GREY,   GREY,   YELLOW, YELLOW, YELLOW}},
      {"shame", {YELLOW, GREY,   GREY,   YELLOW, YELLOW}},
    };

    std::vector<std::string> remain =
      Filter(words, board);

    if (remain.empty()) {
      printf("No words.\n");
    }
    for (const auto &s : remain) {
      printf("%s\n", s.c_str());
    }
    printf("\n------\n");
  }

  {
    {
      auto BLUE = GREEN; // YELLOW;
      auto BROWN = YELLOW;  // GREEN;

      std::vector<std::pair<std::string, std::vector<Color>>>
        board = {
        {"spoof", {BLUE,  GREY,   BROWN,  GREY,  GREY}},
        {"tuber", {GREY,  GREY,   GREY,   GREY,  BROWN}},
        {"using", {GREY,  BROWN,  GREY,   GREY,  GREY}},
        {"famed", {GREY,  GREY,   GREY,   GREY,  GREY}},
        {"fault", {GREY,  GREY,   GREY,   GREY,  GREY}},
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

    printf("\n-------\n");

  }

  ShowPuzzle(
      words,
      "messy",
      {
        // "fight",
        // "trash",
        // "times", "wrong", "rules",
        // "shame",
        "wrong", "rules", "times",
        "shame"
      });


  printf("\n------\n");


  for (const std::string secret : { "sorry", "fails", }) {
    for (const std::string line1 : { "troll", "spoof", }) {
      for (const std::string line2 : { "grant", "tuber", }) {
        for (const std::string line3 : { "using" }) {
          for (const std::string line4 : { "famed", "known", }) {
            for (const std::string line5 : { "error", "fault", }) {

              std::vector<std::pair<std::string, std::vector<Color>>>
                board;

              int green = 0, yellow = 0;
              for (const std::string &line : {
                  line1, line2, line3, line4, line5 }) {
                for (Color c : Color1(secret, line)) {
                  if (c == YELLOW) yellow++;
                  if (c == GREEN) green++;
                }
              }

              if ((green == 3 && yellow == 1) ||
                  (green == 1 && yellow == 3)) {
                printf(AWHITE("Good!") " \n");
                ShowPuzzle(words, secret,
                           {line1, line2, line3, line4, line5 });
              }
            }
          }
        }
      }
    }
  }

  return 0;
}
