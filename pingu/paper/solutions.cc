
#include <string>
#include <cmath>
#include <cstdint>
#include <map>

#include "textsvg.h"
#include "base/stringprintf.h"

#include "encoding.h"
#include "tetris.h"
#include "util.h"

using uint32 = uint32_t;
using int64 = int64_t;

static const char *ShapeTex(Shape s) {
  switch (s) {
  case I_VERT: return "\\ivert";
  case I_HORIZ: return "\\ihoriz";
  case SQUARE: return "\\squarepiece";

  case T_UP: return "\\tup";
  case T_DOWN: return "\\tdown";
  case T_LEFT: return "\\tleft";
  case T_RIGHT: return "\\tright";

  case J_UP: return "\\jup";
  case J_LEFT: return "\\jleft";
  case J_DOWN: return "\\jdown";
  case J_RIGHT: return "\\jright";

  case Z_HORIZ: return "\\zhoriz";
  case Z_VERT: return "\\zvert";

  case S_HORIZ: return "\\shoriz";
  case S_VERT: return "\\svert";

  case L_UP: return "\\lup";
  case L_LEFT: return "\\lleft";
  case L_DOWN: return "\\ldown";
  case L_RIGHT: return "\\lright";
  default: return "ERROR";
  }
}

int main(int argc, char **argv) {
  static constexpr const char *solfile = "../tetris/best-solutions.txt";
  
  std::map<uint8_t, std::vector<Move>> sols =
    Encoding::ParseSolutions(solfile);

  printf("\\begin{itemize}[noitemsep] \\tiny\n");
  constexpr int MAX_BYTE = 256; // XXX
  for (int v = 0; v < MAX_BYTE; v++) {
    auto it = sols.find(v);
    CHECK(it != sols.end()) << "No solution for " << v <<
      " in " << solfile;

    printf("\\item {\\tt %02x}: ", v);
    for (const auto &[shape, col] : it->second) {
      printf("%s %d ", ShapeTex(shape), col);
    }
    printf("\n");
  }
  printf("\\end{itemize}\n");
  
  return 0;
}

