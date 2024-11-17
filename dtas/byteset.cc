
#include "modeling.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <bitset>

#include "base/stringprintf.h"

bool ByteSet64::Contains(uint8_t b) const {
  switch (type) {
    case EMPTY:
      return false;
    case VALUES:
      for (int i = 0; i < 7; i++)
        if (payload[i] == b)
          return true;
      return false;
    case RANGES:
      for (int i = 0; i < 3; i++) {
        uint8_t start = payload[i * 2];
        uint8_t len = payload[i * 2 + 1];
        if (len == 0) continue;
        uint8_t end = start + len - 1;
        // Handle wrap-around:
        if (start <= end) {
          if (b >= start && b <= end) return true;
        } else {
          if (b >= start || b <= end) return true;
        }
      }
      return false;
    default:
      LOG(FATAL) << "Bad ByteSet64 type: " << type;
  }
}

int ByteSet64::Size() const {
  int count = 0;
  // PERF: We can probably make a faster specialized
  // routine for each representation.
  for (int i = 0; i < 256; i++) {
    if (Contains(i)) count++;
  }
  return count;
}

ByteSet64::ByteSet64(const ByteSet& s) {
  if (s.Empty()) {
    type = EMPTY;
    return;
  }

  int count = s.Size();
  if (count <= 7) {
    type = VALUES;
    uint8_t last = 0;
    int i = 0;
    for (uint8_t v : s) {
      payload[i++] = v;
      last = v;
    }
    while (i < 7) payload[i++] = last;
    return;
  }

  // Can't represent a length of 256 for the
  // universal set, so we need two ranges:
  // [0, 255) and [255, 256).
  if (count == 256) {
    type = RANGES;
    payload[0] = 0;
    payload[1] = 255;
    payload[2] = 255;
    payload[3] = 1;
    payload[4] = 0;
    payload[4] = 0;
    return;
  }

  // Otherwise, use nontrivial ranges.
  type = RANGES;
  // This could be smarter; when we fail we don't
  // necessarily fail the best way.

  int range_idx = 0;

  // The code below is conceptually removing elements from the
  // set, but it always does this at the beginning and end.
  // So the working set is s intersected with [lower_bound, upper_bound).
  int lower_bound = 0;
  int upper_bound = 256;

  // First, handle wrap-around. Only one interval
  // needs to use this.
  if (s.Contains(0) && s.Contains(255)) {
    int lo = 255;
    int hi = 0;
    while (s.Contains(hi)) hi++;
    while (s.Contains(lo)) lo--;
    CHECK(hi < 255 && lo > 0 && hi <= lo) << "Since we know "
      "this is not the universal set, there must be some "
      "element between 0 and 255 that is not contained. " <<
      StringPrintf("lo: %02x hi: %02x", lo, hi);
    payload[range_idx * 2] = lo + 1;
    payload[range_idx * 2 + 1] = (hi + 256) - (lo + 1);
    range_idx++;
    lower_bound = hi;
    upper_bound = lo;
  }

  while (range_idx < 3) {
    // Find the next interval, greedily.
    while (lower_bound < upper_bound && !s.Contains(lower_bound))
      lower_bound++;

    // Empty set.
    if (lower_bound >= upper_bound) {
      // Remainder of payload will be zero, which is correct.
      return;
    }

    uint8_t start = lower_bound;
    uint8_t length = 0;
    while (lower_bound < upper_bound && s.Contains(lower_bound)) {
      lower_bound++;
      length++;
    }

    payload[range_idx * 2] = start;
    payload[range_idx * 2 + 1] = length;
    range_idx++;
  }

  // If we ran out of space for ranges, we have to approximate the
  // set. This is the place where we should look for heuristics,
  // but for now, just greedily expand the last interval:
  int last_start = payload[4];
  // Find the largest element in the set. The upper bound is
  // the number *after* that.
  while (!s.Contains(upper_bound - 1)) upper_bound--;
  // Something is wrong if we emitted a range that starts after
  // the largest element??
  CHECK(upper_bound > last_start);
  // Now expand the last range to include the largest number.
  // We already handled the universal set, so the length
  // must be in range.
  payload[5] = upper_bound - last_start;
}
