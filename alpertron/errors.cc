//
// This file is part of Alpertron Calculators.
//
// Copyright 2015-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.
//

#include <string.h>
#include <stdbool.h>
#include "bignbr.h"
#include "globals.h"

int expressionNbr;
void textError(char **pptrOutput, enum eExprErr rc)
{
  char* ptrOut = *pptrOutput;
  switch (rc)
  {
  case EXPR_NUMBER_TOO_LOW:
    copyStr(&ptrOut, "Number too low");
    break;
  case EXPR_NUMBER_TOO_HIGH:
    copyStr(&ptrOut, "Number too high (more than 100000 digits)");
    break;
  case EXPR_INTERM_TOO_HIGH:
    copyStr(&ptrOut, "Intermediate number too high (more than 200000 digits)");
    break;
  case EXPR_DIVIDE_BY_ZERO:
    copyStr(&ptrOut, "Division by zero");
    break;
  case EXPR_PAREN_MISMATCH:
    copyStr(&ptrOut, "Parenthesis mismatch");
    break;
  case EXPR_LITERAL_NOT_INTEGER:
    copyStr(&ptrOut, "Only integer numbers are accepted");
    break;
  case EXPR_SYNTAX_ERROR:
    copyStr(&ptrOut, "Syntax error");
    if (expressionNbr > 0) {
      copyStr(&ptrOut, " in expression #");
      *ptrOut = (char)(expressionNbr + '0');
      ptrOut++;
      *ptrOut = ':';
      ptrOut++;
      *ptrOut = ' ';
      ptrOut++;
      *ptrOut = 0;
    }
    break;
  case EXPR_INVALID_PARAM:
    copyStr(&ptrOut, "Invalid parameter");
    break;
  case EXPR_TOO_FEW_ARGUMENTS:
    copyStr(&ptrOut, "Too few arguments");
    break;
  case EXPR_TOO_MANY_ARGUMENTS:
    copyStr(&ptrOut, "Too many arguments");
    break;
  case EXPR_ARGUMENTS_NOT_RELATIVELY_PRIME:
    copyStr(&ptrOut, "GCD of arguments is not 1");
    break;
  case EXPR_VAR_OR_COUNTER_REQUIRED:
    copyStr(&ptrOut, "Expression #");
    *ptrOut = (char)(expressionNbr + '0');
    ptrOut++;
    copyStr(&ptrOut, " must include the variable <var>x</var> "
            "and/or the counter <var>c</var>");
    break;
  case EXPR_VAR_IN_EXPRESSION:
    copyStr(&ptrOut, "The expression must not include variables");
    break;
  case EXPR_CANNOT_PARSE_EXPRESSION:
    copyStr(&ptrOut, "Internal error: cannot parse expression");
    break;
  default:
    break;
  }
  *pptrOutput = ptrOut;
}
