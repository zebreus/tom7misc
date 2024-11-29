
#include <vector>
#include <string>
#include <utility>
#include <cstdio>

#include "base/logging.h"

using namespace std;

struct Piece {
  Piece(std::vector<std::pair<int, int>> offsets) : offsets(offsets) {}
  // offsets from its home coordinate at 0,0.
  std::vector<std::pair<int, int>> offsets;
  std::pair<int, int> Dimensions() const {
    int w = 0, h = 0;
    for (auto [x, y] : offsets) {
      w = std::max(x, w);
      h = std::max(y, h);
    }
    return make_pair(w, h);
  }
  int Bits() const {
    return offsets.size();
  }
};

// A set of interchangeable pieces (e.g., rotations) with
// a count of how many are available.
struct PiecePool {
  int count = 0;
  vector<int> piece_ids;
};

// Generates z3 script on stdout.
int main(int argc, char **argv) {

  constexpr int QWIDTH = 21, QHEIGHT = 21;

  string qrtext =
    "#######   # # #######"
    "#     #     # #     #"
    "# ### # # #   # ### #"
    "# ### #     # # ### #"
    "# ### #  # ## # ### #"
    "#     #  ###  #     #"
    "####### # # # #######"
    "        # #          "
    "### ##### # ###   #  "
    " ####   ## # # # #  #"
    "###   ### ## ### #  #"
    "## ##    # ### ### ##"
    "  ###### ### ###  #  "
    "        # #   #   # #"
    "####### #   #   #  ##"
    "#     # #     #   ###"
    "# ### # ##  # # # # #"
    "# ### #  ### # # # # "
    "# ### # #  # ### ## #"
    "#     # # #### ### # "
    "####### #  # ### ####";

  vector<Piece> pieces;
  auto AddPiece = [&pieces](Piece p) {
      int id = pieces.size();
      pieces.push_back(p);
      return id;
    };

  // 1x1 dot
  const int dot = AddPiece(Piece({{0,0}}));

  // 2x1 bar, vert and horiz
  const int bar2v = AddPiece(Piece({{0,0}, {0,1}}));
  const int bar2h = AddPiece(Piece({{0,0}, {1,0}}));

  // 3x1 bar, vert and horiz
  const int bar3v = AddPiece(Piece({{0,0}, {0,1}, {0,2}}));
  const int bar3h = AddPiece(Piece({{0,0}, {1,0}, {2,0}}));

  const vector<PiecePool> pools = {
    {.count = 30, .piece_ids = {dot}},
    {.count = 20, .piece_ids = {bar2v, bar2h}},
    {.count = 60, .piece_ids = {bar3v, bar3h}},
  };

  const int num_pieces = pieces.size();

  CHECK(qrtext.size() == QWIDTH * QHEIGHT);

  // Check trivially unsatisfiable, since this seems hard for the
  // solver.
  int qrbits = 0;
  for (char c : qrtext) if (c == '#') qrbits++;
  int covers = 0;
  for (const PiecePool &pool : pools) {
    int maxsize = 0;
    for (int n : pool.piece_ids)
      maxsize = std::max(pieces[n].Bits(), maxsize);
    covers += pool.count * maxsize;
  }
  CHECK(qrbits <= covers) << "Can't cover " << qrbits << " bits in "
    "QR code with pieces that only have " << covers << " bits total";


  // boolean qX_Y gives the target QR code cell's value.
  // These are constant in the theory.
  for (int y = 0; y < QHEIGHT; y++) {
    for (int x = 0; x < QWIDTH; x++) {
      printf("(define-const q%d_%d Bool %s)\n",
             x, y, qrtext[y * QWIDTH + x] == '#' ? "true" : "false");
    }
  }

  // declare variables for each piece.
  // int cN is the number of pieces of that type used.
  // boolean pN_X_Y is true if the top-left of a piece N is at X,Y.
  // boolean mN_X_Y is true if the placements of N cover the square X,Y.
  for (int n = 0; n < num_pieces; n++) {
    printf("(declare-const c%d Int)\n", n);
    for (int y = 0; y < QHEIGHT; y++) {
      for (int x = 0; x < QWIDTH; x++) {
        printf("(declare-const p%d_%d_%d Bool)\n", n, x, y);
        printf("(declare-const m%d_%d_%d Bool)\n", n, x, y);
      }
    }
  }

  auto SelfInterferes = [](const Piece &p, int x0, int y0, int x1, int y1) {
      for (auto [dx0, dy0] : p.offsets) {
        for (auto [dx1, dy1] : p.offsets) {
          if (x0 + dx0 == x1 + dx1 &&
              y0 + dy0 == y1 + dy1) return true;
        }
      }
      return false;
    };

  auto OffBoard = [](const Piece &p, int x, int y) {
      for (auto [dx, dy] : p.offsets)
        if (x + dx >= QWIDTH || y + dy >= QHEIGHT)
          return true;
      return false;
    };

  // self-interference: for a given piece type and position, if the
  // piece is placed there, reject all placements that would have an
  // overlapping square. (interference is symmetric, but we just
  // generate both assertions.) also, reject the placement itself if
  // it would exit the board.
  for (int n = 0; n < num_pieces; n++) {
    const Piece &p = pieces[n];
    // auto [w, h] = p.Dimensions();
    for (int y0 = 0; y0 < QHEIGHT; y0++) {
      for (int x0 = 0; x0 < QWIDTH; x0++) {

        if (OffBoard(p, x0, y0)) {
          // Never legal.
          printf("(assert (not p%d_%d_%d))\n", n, x0, y0);
        } else {
          for (int y1 = 0; y1 < QHEIGHT; y1++) {
            for (int x1 = 0; x1 < QHEIGHT; x1++) {
              if (x0 != x1 || y0 != y1) {
                if (SelfInterferes(p, x0, y0, x1, y1)) {
                  printf("(assert (not (and p%d_%d_%d p%d_%d_%d)))\n",
                         n, x0, y0, n, x1, y1);
                }
              }
            }
          }
        }

      }
    }
  }

  // mask for each piece type. mN_X_Y is true if piece type N is covering
  // the cell at X,Y. derived from pN_X_Y.
  for (int n = 0; n < num_pieces; n++) {
    const Piece &p = pieces[n];
    for (int y0 = 0; y0 < QHEIGHT; y0++) {
      for (int x0 = 0; x0 < QWIDTH; x0++) {
        // the mask for x0,y0 as an or() of all the placements that
        // would set it

        printf("(assert (= m%d_%d_%d (or", n, x0, y0);

        for (int y1 = 0; y1 < QHEIGHT; y1++) {
          for (int x1 = 0; x1 < QHEIGHT; x1++) {
            for (auto [dx, dy] : p.offsets) {
              if (x1 + dx == x0 && y1 + dy == y0) {
                printf(" p%d_%d_%d", n, x1, y1);
              }
            }
          }
        }

        printf(")))\n");
      }
    }
  }

  // The masks may not interfere with one another.
  for (int n = 0; n < num_pieces; n++) {
    for (int m = n + 1; m < num_pieces; m++) {

      for (int y = 0; y < QHEIGHT; y++) {
        for (int x = 0; x < QWIDTH; x++) {
          printf("(assert (not (and m%d_%d_%d m%d_%d_%d)))\n",
                 n, x, y, m, x, y);
        }
      }
    }
  }

  // But together they must equal the qr code.
  for (int y = 0; y < QHEIGHT; y++) {
    for (int x = 0; x < QWIDTH; x++) {
      printf("(assert (= q%d_%d (or", x, y);

      for (int n = 0; n < num_pieces; n++) {
        printf(" m%d_%d_%d", n, x, y);
      }

      printf(")))\n");
    }
  }

  // Count the number of pieces used. cN is just the number of 'true'
  // p variables for the piece type.
  for (int n = 0; n < num_pieces; n++) {
    printf("(assert (= c%d (+", n);
    for (int y = 0; y < QHEIGHT; y++) {
      for (int x = 0; x < QWIDTH; x++) {
        // ite is if-then-else, i.e. the ?: operator.
        // we count 1 if the position is true, otherwise 0
        printf(" (ite p%d_%d_%d 1 0)", n, x, y);
      }
    }
    printf(")))\n");
  }

  // Finally, constraints on the counts.
  for (const PiecePool &pool : pools) {
    printf("(assert (>= %d (+", pool.count);
    for (int n : pool.piece_ids) {
      printf(" c%d", n);
    }
    printf(")))\n");
  }

  printf("\n(check-sat)\n"
         "(get-model)\n");

  return 0;
}
