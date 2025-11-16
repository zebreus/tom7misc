
#include "re2-util.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "re2/re2.h"
#include "base/logging.h"

std::string RE2Util::MapReplacement(
    std::string_view source,
    const RE2 &pattern,
    const std::function<std::string(std::span<const std::string_view>)> &f) {
  std::string result;
  // Guess at the length.
  result.reserve(source.length());

  // 1 for the full match.
  const int num_submatches = 1 + pattern.NumberOfCapturingGroups();
  std::vector<std::string_view> submatches(num_submatches);

  while (pattern.Match(source, 0, source.length(),
                       RE2::UNANCHORED, submatches.data(), num_submatches)) {
    std::string_view full_match = submatches[0];
    CHECK(full_match.data() != nullptr && !full_match.empty()) << "The "
      "pattern may not match an empty substring.";

    size_t match_start_pos = full_match.data() - source.data();

    // Prefix
    result.append(source.substr(0, match_start_pos));
    // Replacement string
    result.append(f(submatches));

    source.remove_prefix(match_start_pos + full_match.size());
  }

  // Any content after the last match.
  if (!source.empty()) {
    result.append(source);
  }

  return result;
}
