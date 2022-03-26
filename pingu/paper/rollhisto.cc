
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <unordered_set>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"
#include "color-util.h"
#include "textsvg.h"
#include "util.h"

#include "tetris.h"
#include "nes-tetris.h"
#include "encoding.h"

// For any starting LFSR+last_drop state, what's the largest number of
// iterations we need before seeing all values of rng1 & 7?
void AllFirstRolls() {

  constexpr int BUCKETS = 100;
  
  std::vector<int> counts;
  for (int i = 0; i < BUCKETS; i++) counts.push_back(0);
  
  int max_count = 0;

  // Loop over the whole period.
  // We can't just do this by setting rng1 and rng2,
  // because some values are degenerate (e.g. 0000).
  RNGState outer_state;
  for (int x = 0; x < 32768; x++) {

    for (int c = 0; c < 256; c++) {
      RNGState state = outer_state;
      state.drop_count = c;
      
      RNGState start_state = state;
        
      uint8 got = 0;

      for (int count = 0; count < 32768; count++) {
        int roll = (state.rng1 + state.drop_count) & 7;
        got |= (1 << roll);
        /*
        printf("%s Rolled %d. got = %d\n",
               RNGString(state).c_str(), roll, got);
        */

        if (got == 0b11111111) {
          CHECK(count < (int)counts.size());
          counts[count]++;
          max_count = std::max(count, max_count);
          // printf("OK in %d\n", max_count);
          goto next;
        }

        state = FastNextRNG(state);
      }
      LOG(FATAL) << "Never got all rolls?! " << RNGString(start_state);
    next:;
    }

    outer_state = FastNextRNG(outer_state);
  }


  for (int i = 0; i < (int)counts.size(); i++) {
    printf("%d: %d\n", i, 1 + counts[i]);
  }
  /*
  printf("max_count: %d\n", max_count);
  */

  string svg = TextSVG::Header(BUCKETS, 28);

  int max_bucket = 0;
  for (int v : counts) max_bucket = std::max(max_bucket, v);
  
  for (int b = 0; b < BUCKETS; b++) {
    double f = counts[b] / (double)max_bucket;
    double height = std::max(0.01, 24.0 * f);
    StringAppendF(&svg,
                  "<rect x=\"%.3f\" y=\"%.3f\" "
                  "width=\"0.80\" height=\"%.3f\" />\n",
                  (double)b, 24 - height, height);
  }

  for (int b = 0; b <= BUCKETS; b++) {
    if ((b + 1) % 5 == 0 && (b + 1) < 100) {
      StringAppendF(
          &svg,
          "%s\n",
          TextSVG::Text(b, 24.2 + 3.2, "sans-serif", 3.2,
                        {{"#000", StringPrintf("%d", b + 1)}}).c_str());
    }
  }
  
  svg += TextSVG::Footer();
  Util::WriteFile("rollhisto.svg", svg);
}

int main(int argc, char **argv) {
  AllFirstRolls();
  return 0;
}
