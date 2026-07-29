#include "z3.h"

#include <cmath>
#include <string_view>
#include <optional>
#include <string>
#include <format>

#include "base/logging.h"
#include "crypt/sha256.h"
#include "process-util.h"
#include "util.h"


Z3::SatResult Z3::RunSat(std::string_view content,
                         std::optional<double> timeout_seconds) {
  std::string filename =
    std::format("runz3-{}.z3",
                SHA256::Ascii(SHA256::HashStringView(content)));
  Util::WriteFile(filename, content);
  std::string targ;
  if (timeout_seconds.has_value()) {
    // Milliseconds
    targ = std::format("-t:{}", (int)std::round(
                           timeout_seconds.value() * 1000.0));
  }

  std::optional<std::string> z3result =
    ProcessUtil::GetOutput(std::format("d:\\z3\\bin\\z3.exe {} {}",
                                       targ,
                                       filename));
  // Could return UNKNOWN here, but that would probably be surprising.
  CHECK(z3result.has_value()) << "Couldn't run z3?";

  // Just remember that "sat" is in "unsat"!
  if (Util::StrContains(z3result.value(), "unknown")) {
    (void)Util::RemoveFile(filename);
    return SatResult::UNKNOWN;
  }
  if (Util::StrContains(z3result.value(), "unsat")) {
    (void)Util::RemoveFile(filename);
    return SatResult::UNSAT;
  }
  if (Util::StrContains(z3result.value(), "sat")) {
    (void)Util::RemoveFile(filename);
    return SatResult::SAT;
  }
  LOG(FATAL) << "Unparseable z3 result:\n" << z3result.value() <<
    "On input file: " << filename;
}
