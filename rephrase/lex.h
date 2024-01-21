
#ifndef _REPHRASE_LEX_H
#define _REPHRASE_LEX_H

#include <utility>
#include <string>
#include <vector>

// A token, associated with some span of bytes in the input. Sometimes
// the contents of the span is obvious, like LPAREN always refers to a
// single ( byte. In the case of something like a string literal, the
// associated span lets us read off the contents of the span. The
// tokenizer skips whitespace except where it is meaningful (e.g. inside
// layout or string literals).
enum TokenType {
  LPAREN,
  RPAREN,
  LBRACKET,
  RBRACKET,
  COMMA,
  PERIOD,
  UNDERSCORE,
  EQUALS,
  BAR,
  TIMES,
  // ->
  ARROW,
  // =>
  DARROW,

  // Keywords.
  FN,
  FUN,
  VAL,
  DO,
  LET,
  IN,
  END,

  IF,
  THEN,
  ELSE,
  ANDALSO,
  ORELSE,

  DATATYPE,
  OF,
  CASE,

  // Identifier.
  // This can be alphanumeric or symbolic.
  ID,

  // like 1234
  DIGITS,

  // like 0x2A03 or 0u09DE
  NUMERIC_LIT,
  // like 1e100 or .4
  FLOAT_LIT,
  LAYOUT_LIT,
  STR_LIT,

  INVALID,
};

struct Token {
  Token(TokenType type, size_t start, size_t length) :
    type(type), start(start), length(length) {}
  TokenType type = INVALID;
  // These refer into the input buffer.
  size_t start = 0;
  size_t length = 0;
};

const char *TokenTypeString(TokenType tok);

std::vector<Token> Lex(const std::string &input_string);

// Returns the source string (with ANSI colors) and the sequence of
// tokens (with ANSI colors).
std::pair<std::string, std::string> ColorTokens(
    const std::string &input_string,
    const std::vector<Token> &tokens);


#endif
