
#include <string>

#include "base/print.h"
#include "base/logging.h"
#include "ansi.h"
#include "chess.h"

int main(int argc, char **argv) {
  ANSI::Init();

  std::string fen;
  for (int a = 1; a < argc; a++) {
    if (a != 1) fen.push_back(' ');
    fen.append(argv[a]);
  }

  Position pos;
  CHECK(Position::ParseFEN(fen, &pos));
  Print("\n{}\n\n", pos.UnicodeAnsiBoardString());

  return 0;
}
