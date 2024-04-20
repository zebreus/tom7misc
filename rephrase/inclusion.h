
#ifndef _REPHRASE_INCLUSION_H
#define _REPHRASE_INCLUSION_H

#include <cstddef>
#include <vector>
#include <string>
#include <tuple>

#include "interval-cover.h"
#include "lexing.h"

// Processes includes.
// After lexing, we replace the sequence IMPORT STR_LIT with
// the contents of the named file, which requires loading
// and lexing that file (possibly recursively processing its
// imports) and then assembling the tokens and strings into a
// single object.
//
// The semantics of includes are as follows:
//   - If a file includes itself (including transitively), the
//     program is ill-formed and this code aborts.
//   - If a file is included a second time, but not in a cycle,
//     the include has no effect.

// The source code is rendered to a single string. This describes
// the provenance of the bytes in that string.
struct SourceMap {
  // For each position in the string, its original source file.
  IntervalCover<std::string> filecover;
  // For each position in the string, the line (within the source file
  // above) that contains the byte. These are used for error messages
  // so they are 1-based by convention.
  IntervalCover<int> linecover;

  // Source files may not exceed 100 terabytes.
  static constexpr size_t BOGUS_POS = 100'000'000'000'000;
};

struct Inclusion {

  static std::tuple<std::string, std::vector<el::Token>, SourceMap>
  Process(const std::vector<std::string> &includepaths,
          const std::string &filename);

  // When testing, we often want to work with a literal string. This
  // lets us generate a working sourcemap for that string (coming from
  // the "file", which need not be real).
  static SourceMap SimpleSourceMap(const std::string &file,
                                   const std::string &content);
};


#endif
