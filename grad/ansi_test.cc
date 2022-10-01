

#include "ansi.h"

#include <cstdio>

#include "chess.h"

int main(int argc, char **argv) {
  AnsiInit();
  printf("NORMAL" " "
         ARED("ARED") " "
         AGREY("AGREY") " "
         ABLUE("ABLUE") " "
         ACYAN("ACYAN") " "
         AYELLOW("AYELLOW") " "
         AGREEN("AGREEN") " "
         AWHITE("AWHITE") " "
         APURPLE("APURPLE") "\n");

  printf(AFGCOLOR(50, 74, 168, "Blue FG") " "
         ABGCOLOR(50, 74, 168, "Blue BG") " "
         AFGCOLOR(226, 242, 136, ABGCOLOR(50, 74, 168, "Combined")) "\n");

  {
    printf("Piece: ♟\n");

    Position pos;
    printf("Unicode:\n%s\n", pos.UnicodeBoardString(true).c_str());

    printf("Ansi:\n%s\n", pos.UnicodeAnsiBoardString().c_str());
  }

  return 0;
}
