
#include <stdio.h>

#include "base/logging.h"

int main(int argc, char **argv) {

  // How many patterns fit?

  for (int pattern_width = 1; pattern_width < 8; pattern_width++) {
    for (int src_width = 1; src_width < 50; src_width++) {
      for (int occurrence_x_stride = 1; occurrence_x_stride < 40;
           occurrence_x_stride++) {



        // preconditions
        if (pattern_width > src_width) continue;
        if (occurrence_x_stride > src_width) continue;

        const int formula =
          ((src_width - pattern_width) + 1) / occurrence_x_stride;


        int count = 0;
        int xpos = 0;
        // pattern_width-1 is the last index we actually read
        while (xpos + (pattern_width - 1) < src_width) {
          // ok position
          count++;
          xpos += occurrence_x_stride;
        }

        if (formula != count) {
          printf("Wrong for\n"
                 "   pattern_width %d\n"
                 "   src_width %d\n"
                 "   occurrence_x_stride %d\n"
                 "... formula %d, count %d\n",
                 pattern_width, src_width, occurrence_x_stride,
                 formula, count);
        }
      }
    }
  }
  printf("OK\n");
  return 0;
}
