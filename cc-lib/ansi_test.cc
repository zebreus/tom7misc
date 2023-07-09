

#include "ansi.h"

#include <cstdio>

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

  return 0;
}
