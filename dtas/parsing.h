
#ifndef _TOKENIZE_H
#define _TOKENIZE_H

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "formula.h"

enum TokenType {
  // Singleton symbols
  COMMA,
  EQUALS,
  LESS,
  LESSEQ,
  GREATER,
  GREATEREQ,
  PLUS,
  MINUS,
  HASH,
  COLON,
  PERIOD,
  LPAREN,
  RPAREN,
  LBRACE,
  RBRACE,
  ARROW,
  LSQUARE,
  RSQUARE,

  // Symbolic keywords.
  // Note that .directives and instructions are not keywords.
  // This is just used for the formula language.
  IN,

  // With payload.
  // In any format; parsed into the number it denotes.
  NUMBER,
  // any alphanumeric symbol,
  // including that are part of directives, opcodes, register names,
  // labels, etc.
  SYMBOL,
  // Until end of line. Doesn't include the comment chars.
  COMMENT,
};

struct Token {
  TokenType type;

  int64_t num = 0;
  std::string str;
};

inline constexpr Token SimpleToken(TokenType t) {
  return Token{.type = t};
}

std::string TokenTypeString(TokenType t);

// This is a line-based syntax, so we tokenize and parse a line
// at a time.
std::vector<Token> Tokenize(int line_num,
                            const std::string &input_string);

enum class ExpType {
  LABEL,
  HIGH_BYTE,
  LOW_BYTE,
  PLUS,
  MINUS,
  NUMBER,
};

struct Exp {
  ExpType type;
  std::string label;
  std::shared_ptr<Exp> a, b;
  int64_t number = 0;
};

std::string ExpString(const Exp *e);

// This is all possible addressing modes; not all are
// available for each mnemonic!
struct Addressing {
  enum Type {
    // e.g. a parse error
    INVALID,
    // e.g. ROR A
    ACCUMULATOR,
    // immediate. has an expression that should
    // evaluate to an 8-bit value.
    IMMEDIATE,
    // A specific address. Has an expression that
    // evaluates to that 16-bit value.
    //
    // This covers both zero-page addressing and
    // 16-bit addressing, which have different
    // opcodes. But they are only distinguished
    // in the syntax by the specific value, which
    // requires evaluating the expression.
    ADDR,
    // address + x (which is written addr,x)
    ADDR_X,
    // address + y (written addr,y)
    ADDR_Y,
    // e.g. JMP ($fffc)
    // Denotes the 16-bit value (e.g. target of the jump)
    // that is contained at the 16-bit address.
    INDIRECT,

    // e.g. LDA ($42,X)
    // Compute the indirect address by first adding X,
    // then loading.
    // Has an expression with an 8-bit value, which would typically be
    // the beginning of a table of addresses in the zero page.
    INDIRECT_X,
    // e.g. LDA ($42),Y
    // First get the indirect address from the zero page, then add y.
    // Has an expression with an 8-bit value, which contains a 16-bit
    // address (typically a pointer to a struct or array) in the
    // the zero page.
    INDIRECT_Y,
  };

  Type type = ACCUMULATOR;
  std::shared_ptr<Exp> exp;

  Addressing() : type(INVALID) {}
  Addressing(Type t) : type(t) {}
  Addressing(Type t, std::shared_ptr<Exp> exp_in) :
    type(t), exp(std::move(exp_in)) {}
};

struct Line {
  enum class Type {
    NOTHING,
    DIRECTIVE_INDEX,
    DIRECTIVE_MEM,
    DIRECTIVE_ORG,
    DIRECTIVE_DB,
    DIRECTIVE_DW,

    CONSTANT_DECL,
    INSTRUCTION,

    // Meta-instructions to model checker.
    DIRECTIVE_ALWAYS,
    DIRECTIVE_HERE,
  };

  Type type = Type::NOTHING;
  int num = 0;
  std::string symbol;
  Addressing addressing;
  std::vector<std::shared_ptr<Exp>> exps;
  std::shared_ptr<Form> formula;
};

void StripComments(std::vector<Token> *tokens,
                   const std::function<std::string()> &Error);

std::optional<std::string> ConsumeLabel(
    std::vector<Token> *tokens,
    const std::function<std::string()> &Error);

Line ParseLine(const std::vector<Token> &tokens,
               const std::function<std::string()> &Error);

#endif
