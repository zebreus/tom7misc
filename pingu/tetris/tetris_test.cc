
#include "tetris.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

using Tetris = TetrisDepth<8>;

static void TestDrops() {
  {
    Tetris tetris;
    CHECK(tetris.Place(L_DOWN, 1));
    CHECK(tetris.Place(Z_HORIZ, 1));
    CHECK(tetris.Place(S_HORIZ, 2));
    CHECK(tetris.Place(I_VERT, 0)) << tetris.BoardString();

    CHECK_EQ(tetris.BoardString(),
             "|..........|\n"
             "|#..##.....|\n"
             "|#.##......|\n"
             "|###.......|\n"
             "|#.##......|\n"
             "|###.......|\n"
             "|###.......|\n"
             "|.##.......|\n");
    CHECK(!tetris.Place(I_VERT, 0));
    CHECK(!tetris.Place(T_LEFT, 0));
    CHECK(tetris.Place(SQUARE, 1));
    CHECK(tetris.Place(I_HORIZ, 4));
    CHECK_EQ(tetris.BoardString(),
             "|.##.####..|\n"
             "|#####.....|\n"
             "|#.##......|\n"
             "|###.......|\n"
             "|#.##......|\n"
             "|###.......|\n"
             "|###.......|\n"
             "|.##.......|\n");
    CHECK(!tetris.Place(SQUARE, 7));
    // (no drop on ground)
    CHECK(!tetris.Place(SQUARE, 8));
    CHECK(!tetris.Place(SQUARE, 9));
  }

  {
    Tetris tetris;
    tetris.ClearBoard();
    tetris.SetRowString("###.......", Tetris::MAX_DEPTH - 8);
    tetris.SetRowString("##........", Tetris::MAX_DEPTH - 7);
    tetris.SetRowString("#.........", Tetris::MAX_DEPTH - 6);
    tetris.SetRowString("#.........", Tetris::MAX_DEPTH - 5);
    tetris.SetRowString("#.........", Tetris::MAX_DEPTH - 4);
    tetris.SetRowString("#.........", Tetris::MAX_DEPTH - 3);
    tetris.SetRowString("##........", Tetris::MAX_DEPTH - 2);
    tetris.SetRowString(".#........", Tetris::MAX_DEPTH - 1);
    CHECK(!tetris.Place(I_HORIZ, 0));
  }
}

static void TestLines() {
  {
    Tetris tetris;
    tetris.ClearBoard();
    tetris.SetRowString(".##.##..##", Tetris::MAX_DEPTH - 6);
    tetris.SetRowString("#######.##", Tetris::MAX_DEPTH - 5);
    tetris.SetRowString("#........#", Tetris::MAX_DEPTH - 4);
    tetris.SetRowString("#######.##", Tetris::MAX_DEPTH - 3);
    tetris.SetRowString("......#.#.", Tetris::MAX_DEPTH - 2);
    tetris.SetRowString("......###.", Tetris::MAX_DEPTH - 1);

    CHECK(tetris.Place(I_VERT, 7));
    CHECK_EQ(tetris.BoardString(),
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|.##.##..##|\n"
             "|#......#.#|\n"
             "|......###.|\n"
             "|......###.|\n");
  }


  {
    // Piece can land on bottom if supported; make sure we
    // don't read out of bounds etc.
    Tetris tetris;
    tetris.ClearBoard();
    tetris.SetRowString("..#.#.#.#.", Tetris::MAX_DEPTH - 4);
    tetris.SetRowString("..########", Tetris::MAX_DEPTH - 3);
    tetris.SetRowString(".#########", Tetris::MAX_DEPTH - 2);
    tetris.SetRowString(".#########", Tetris::MAX_DEPTH - 1);
    CHECK(tetris.Place(J_DOWN, 0));
    CHECK_EQ(tetris.BoardString(),
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..........|\n"
             "|..#.#.#.#.|\n");
  }
}

int main(int argc, char **argv) {
  TestDrops();
  TestLines();
  printf("OK\n");
  return 0;
}
