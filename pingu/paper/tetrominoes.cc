
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

static std::vector<std::pair<int, int>> ShapeVec(Shape s) {
  switch (s) {
  case I_VERT: return {{0, 0}, {0, 4}, {1, 4}, {1, 0}};
  case I_HORIZ: return {{0, 0}, {0, 1}, {4, 1}, {4, 0}};
  case SQUARE: return {{0, 0}, {0, 2}, {2, 2}, {2, 0}};
  case T_UP: return {{0, 0}, {0, 1}, {1, 1}, {1, 2}, {2, 2}, {2, 1}, {3, 1}, {3, 0}};
  case T_DOWN: return {{0, 1}, {0, 2}, {3, 2}, {3, 1}, {2, 1}, {2, 0}, {1, 0}, {1, 1}};
  case T_LEFT: return {{0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 3}, {2, 0}, {1, 0}, {1, 1}};
  case T_RIGHT: return {{0, 0}, {0, 3}, {1, 3}, {1, 2}, {2, 2}, {2, 1}, {1, 1}, {1, 0}};
  case J_UP: return {{0, 0}, {0, 1}, {1, 1}, {1, 3}, {2, 3}, {2, 0}};
  case J_LEFT: return {{0, 1}, {0, 2}, {3, 2}, {3, 0}, {2, 0}, {2, 1}};
  case J_DOWN: return {{0, 0}, {0, 3}, {2, 3}, {2, 2}, {1, 2}, {1, 0}};
  case J_RIGHT: return {{0, 0}, {0, 2}, {1, 2}, {1, 1}, {3, 1}, {3, 0}};
  case Z_HORIZ: return {{0, 1}, {0, 2}, {2, 2}, {2, 1}, {3, 1}, {3, 0}, {1, 0}, {1, 1}};
  case Z_VERT: return {{0, 0}, {0, 2}, {1, 2}, {1, 3}, {2, 3}, {2, 1}, {1, 1}, {1, 0}};
  case S_HORIZ: return {{0, 0}, {0, 1}, {1, 1}, {1, 2}, {3, 2}, {3, 1}, {2, 1}, {2, 0}};
  case S_VERT: return {{0, 1}, {0, 3}, {1, 3}, {1, 2}, {2, 2}, {2, 0}, {1, 0}, {1, 1}};
  case L_UP: return {{0, 0}, {0, 3}, {1, 3}, {1, 1}, {2, 1}, {2, 0}};
  case L_LEFT: return {{0, 0}, {0, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 0}};
  case L_DOWN: return {{0, 2}, {0, 3}, {2, 3}, {2, 0}, {1, 0}, {1, 2}};
  case L_RIGHT: return {{0, 0}, {0, 2}, {3, 2}, {3, 1}, {1, 1}, {1, 0}};
  default: return {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
  }
}

static void MakeShape(Shape s, string file) {
  std::vector<std::pair<int, int>> pts = ShapeVec(s);

  int width = 0;
  for (auto [x, y] : pts) width = std::max(width, x);
  
  string out = TextSVG::Header(width, 4.0);
  StringAppendF(&out, "<polygon fill=\"#000\" points=\"");
  for (auto [x, y] : pts) {
    StringAppendF(&out, "%d,%d ", x, 4 - y);
  }
  StringAppendF(&out, "\" />\n");
  StringAppendF(&out, "%s", TextSVG::Footer().c_str());

  Util::WriteFile(file, out);
}

int main(int argc, char **argv) {

  MakeShape(I_VERT, "i_vert.svg");
  MakeShape(I_HORIZ, "i_horiz.svg");
  MakeShape(SQUARE, "square.svg");

  MakeShape(T_UP, "t_up.svg");
  MakeShape(T_DOWN, "t_down.svg");
  MakeShape(T_LEFT, "t_left.svg");
  MakeShape(T_RIGHT, "t_right.svg");

  MakeShape(J_UP, "j_up.svg");
  MakeShape(J_LEFT, "j_left.svg");
  MakeShape(J_DOWN, "j_down.svg");
  MakeShape(J_RIGHT, "j_right.svg");

  MakeShape(Z_HORIZ, "z_horiz.svg");
  MakeShape(Z_VERT, "z_vert.svg");

  MakeShape(S_HORIZ, "s_horiz.svg");
  MakeShape(S_VERT, "s_vert.svg");

  MakeShape(L_UP, "l_up.svg");
  MakeShape(L_LEFT, "l_left.svg");
  MakeShape(L_DOWN, "l_down.svg");
  MakeShape(L_RIGHT, "l_right.svg");

  return 0;
}
