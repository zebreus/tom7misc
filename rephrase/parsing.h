
#ifndef _REPHRASE_PARSING_H
#define _REPHRASE_PARSING_H

#include <string>
#include <vector>

#include "el.h"
#include "inclusion.h"
#include "lexing.h"

namespace el {

struct Parsing {
  // May return nullptr if parsing fails, but there are also many
  // cases where it aborts internally.
  static const Exp *Parse(AstPool *pool,
                          const SourceMap &source_map,
                          // Raw input string.
                          const std::string &input,
                          // Tokenization of the input (via Lexing::Lex).
                          const std::vector<Token> &tokens);
};

}  // namespace el

#endif

