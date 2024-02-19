
#ifndef _REPHRASE_INCLUSION_H
#define _REPHRASE_INCLUSION_H

#include <vector>
#include <string>
#include <tuple>

#include "interval-cover.h"
#include "lex.h"

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
  IntervalCover<std::string> cover;
};

struct Inclusion {

  static std::tuple<std::string, std::vector<el::Token>, SourceMap>
  Process(const std::vector<std::string> &includepaths,
          const std::string &filename);

};


#endif
