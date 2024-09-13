
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
static constexpr int report_threshold[10] = {
  999999999,      // 0
  999999999,      // 1
  90000,          // 2
  100,            // 3
  35,             // 4
  8,              // 5
  9,              // 6
  20,             // 7
  100,            // 8
  100,            // 9
};

// Threshold to save to disk.
static inline constexpr int save_threshold[10] = {
  999999999,    // 0
  999999999,    // 1
  5000,         // 2
  35,           // 3
  20,           // 4
  5,            // 5
  6,            // 6
  10,           // 7
  15,           // 8
  16,           // 9
};

#endif

