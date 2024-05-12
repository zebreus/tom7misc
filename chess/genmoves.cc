
#include "chess.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using Move = Position::Move;

// Generates board positions with moves for testing.
// Each line is the position in FEN notation, then the complete set of
// long move strings that are valid in that position.
static void GenMoves(int seed) {
  static constexpr int NUM = 100;

  for (int i = 0; i < NUM; i++) {
    Position pos = Position::RandomLegalPos(seed * 123456789 + i);
    std::vector<Move> moves = pos.GetLegalMoves();
    printf("%s\n ", pos.ToFEN(0, 0).c_str());
    for (const Move &m : moves) {
      printf(" %s", Position::DebugMoveString(m).c_str());
    }
    printf("\n");
  }

}

int main(int argc, char **argv) {
  const int seed = argc >= 2 ? atoi(argv[1]) : 0;

  GenMoves(seed);

  return 0;
}
