
#ifndef _BRUTE_UTIL_H
#define _BRUTE_UTIL_H

static inline constexpr int best_threshold[10] = {
  // best with 0 non-squares
  999999999,
  // best with 1 non-square
  999999999,
  // best with 2 non-squares
  333,
  // best with 3 non-squares
  25,
  // best with 4 non-squares
  15,
  // best with 5 non-squares
  5,
  // best with 6 non-squares
  6,
  // best with 7 non-squares
  9,
  // 8
  12,
  // 9
  14,
};

// Threshold to add to recent results.
static inline constexpr int report_threshold[10] = {
  999999999,      // 0
  999999999,      // 1
  90000,          // 2
  5000,           // 3
  3000,           // 4
  500,            // 5
  500,            // 6
  400,            // 7
  400,            // 8
  400,            // 9
};

// Threshold to save to disk.
static inline constexpr int save_threshold[10] = {
  999999999,    // 0
  999999999,    // 1
  1000,         // 2
  35,           // 3
  20,           // 4
  5,            // 5
  6,            // 6
  13,           // 7
  22,           // 8
  28,           // 9
};

#endif

