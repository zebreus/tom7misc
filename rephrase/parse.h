
#ifndef _REPHRASE_PARSE_H
#define _REPHRASE_PARSE_H

#include <string>

#include "ast.h"
#include "lex.h"

struct Parsing {
  static const Exp *Parse(AstPool *pool,
                          // Raw input string.
                          const std::string &input,
                          // Tokenization of the input (via Lexing::Lex).
                          const std::vector<Token> &tokens);
};

#endif

