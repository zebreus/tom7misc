
#ifndef _REPHRASE_PARSE_H
#define _REPHRASE_PARSE_H

#include <string>

#include "ast.h"

const Exp *Parse(AstPool *pool, const std::string &input);

#endif

