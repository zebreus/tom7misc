
#include "lex.h"

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

#include "ansi.h"
#include "re2/re2.h"
#include "base/logging.h"
#include "base/stringprintf.h"

static constexpr bool VERBOSE = false;

namespace el {

const char *TokenTypeString(TokenType tok) {
  switch (tok) {
  // These characters can't be part of identifiers.
  case LPAREN: return "LPAREN";
  case RPAREN: return "RPAREN";
  case LBRACKET: return "LBRACKET";
  case RBRACKET: return "RBRACKET";
  case LBRACE: return "LBRACE";
  case RBRACE: return "RBRACE";
  case COMMA: return "COMMA";
  case PERIOD: return "PERIOD";

  // Keywords.
  case FN: return "FN";
  case VAL: return "VAL";
  case DO: return "DO";
  case FUN: return "FUN";
  case LET: return "LET";
  case IN: return "IN";
  case END: return "END";
  case AS: return "AS";
  case AND: return "AND";
  case FAIL: return "FAIL";

  // Symbolic keywords.
  case TIMES: return "TIMES";
  case ARROW: return "ARROW";
  case DARROW: return "DARROW";
  case COLON: return "COLON";
  case SEMICOLON: return "SEMICOLON";
  case UNDERSCORE: return "UNDERSCORE";
  case EQUALS: return "EQUALS";
  case BAR: return "BAR";

  case TRUE: return "TRUE";
  case FALSE: return "FALSE";
  case IF: return "IF";
  case THEN: return "THEN";
  case ELSE: return "ELSE";
  case ANDALSO: return "ANDALSO";
  case ORELSE: return "ORELSE";
  case ANDTHEN: return "ANDTHEN";
  case OTHERWISE: return "OTHERWISE";

  case DATATYPE: return "DATATYPE";
  case OF: return "OF";
  case CASE: return "CASE";
  case IMPORT: return "IMPORT";

  case OBJECT: return "OBJECT";
  case WITH: return "WITH";
  case WITHOUT: return "WITHOUT";

  // Identifier.
  case ID: return "ID";

  case NUMERIC_LIT: return "NUMERIC_LIT";
  case FLOAT_LIT: return "FLOAT_LIT";
  case DIGITS: return "DIGITS";
  case LAYOUT_LIT: return "LAYOUT_LIT";
  case STR_LIT: return "STR_LIT";

  case LAYOUT_COMMENT: return "LAYOUT_COMMENT";

  case INVALID: return "INVALID";

  default: return "???UNHANDLED???";
  }
}


// Background colors to highlight tokens.
static std::array<uint32_t, 6> COLORS = {
  0x000066FF,
  0x006600FF,
  0x660000FF,
  0x660066FF,
  0x006666FF,
  0x666600FF,
};

std::pair<std::string, std::string> Lexing::ColorTokens(
    const std::string &input_string,
    const std::vector<Token> &tokens) {
  std::string source, ctokens;
  int color_idx = 0;
  size_t input_pos = 0;
  for (int t = 0; t < (int)tokens.size(); t++) {
    const Token &tok = tokens[t];

    if (VERBOSE) {
      printf("in_pos %zu. tok %d %s from %zu for %zu\n", input_pos,
             t, TokenTypeString(tok.type),
             tok.start, tok.length);
    }

    CHECK(input_pos <= tok.start);
    CHECK(tok.start < input_string.size()) << input_string.size();
    CHECK(tok.start + tok.length <= input_string.size()) <<
      input_string.size();
    // If there is whitespace to skip, emit it with black bg.
    if (input_pos < tok.start) {
      source += ANSI::BackgroundRGB(0x22, 0x22, 0x22);
      // StringAppendF(&source, "%d", tok.start - input_pos);
      while (input_pos < tok.start) {
        source.push_back(input_string[input_pos]);
        input_pos++;
      }
    }

    CHECK(input_pos == tok.start);
    const uint32_t c = COLORS[color_idx];
    color_idx++;
    color_idx %= COLORS.size();

    // TODO: Add newlines to color tokens.
    std::string bc = ANSI::BackgroundRGB32(c);
    std::string fc = ANSI::ForegroundRGB32(c | 0x808080FF);
    std::string in = input_string.substr(tok.start, tok.length);
    StringAppendF(&source, "%s%s", bc.c_str(), in.c_str());
    StringAppendF(&ctokens, "%s%s" ANSI_RESET " ",
                  fc.c_str(), TokenTypeString(tok.type));
    input_pos += tok.length;
  }
  source += ANSI_RESET;
  ctokens += ANSI_RESET;
  return std::make_pair(source, ctokens);
}

// Lexing.
std::optional<std::vector<Token>> Lexing::Lex(
    const std::string &input_string, std::string *error) {
  static const RE2 whitespace("[ \r\n\t]+");

  // Numeric literals of various sorts.
  // When a 0 prefix, we have hex (x), binary (b),
  // decimal (d) and octal (o). We also have unicode
  // character literals (u). Note that octal is not
  // allowed with just a leading 0, as C does.
  // In each case, the . character is allowed (and
  // ignored) as a separator.
  static const RE2 explicit_numeric_lit(
      "(?:"
      // hex prefix
      "(?:0[Xx][0-9A-Fa-f.]+)"
      "|"
      // binary prefix
      "(?:0[Bb][01.]+)"
      "|"
      // octal prefix.
      "(?:0[Oo][0-7.]+)"
      "|"
      // unicode prefix (char literal)
      "(?:0[Uu][0-9A-Fa-f.]+)"
      ")");

  // In 1e100, the "e100" part.
  #define EXPONENT_SUFFIX "(?:[Ee][-+]?[0-9]+)"

  static const RE2 float_lit(
      "(?:"
      // with optional leading digits
      "[-+]?[0-9]*[.][0-9]+" EXPONENT_SUFFIX "?"
      "|"
      // with optional trailing digits
      "[-+]?[0-9]+[.][0-9]*" EXPONENT_SUFFIX "?"
      "|"
      // with no decimal point, but required suffix
      "[-+]?[0-9]+" EXPONENT_SUFFIX
      ")");

  // Then we try to recognize negative decimal numbers.
  static const RE2 negative_decimal(
      "(?:-[0-9]+)");

  // Then plain digits, which can be used in some places
  // where negative integers cannot.
  static const RE2 digits("[0-9]+");

  // TODO: Allow UTF-8
  // Note that - and _ can appear in alphanumeric-identifiers
  // as well as symbolic ones.
  // Note that . can appear in symbolic identifiers, but not as the
  // first character.
  #define ALPHA_IDENT "(?:[A-Za-z][-'_A-Za-z0-9]*)"
  #define SYMBOLIC_IDENT "(?:[-~`!@#$%^&*=_+|:<>?/][-~`!@#$%^&*=_+|:<>?/.]*)"
  static const RE2 ident("(" ALPHA_IDENT "|" SYMBOLIC_IDENT ")");
  static const RE2 strlit(
    // Starting with double quote
    "\""
    // Then some number of characters.
    "(?:"
    // Any character other than " or newline or backslash.
    R"([^"\\\r\n])"
    "|"
    // Or an escaped character. This is permissive; we parse
    // the string literals and interpret escape characters
    // separately.
    R"(\\.)"
    ")*"
    "\"");

  static const RE2 start_comment("\\(\\*");

  // Regex for the interior of a [layout literal].
  // When we use antiquote for a nested expression, like
  // [aaa[exp]bbb], aaa and bbb are lexed as separate tokens.
  // Unlike string literals, which include their quotation
  // marks in the token, when we lex a layout literal we
  // also output tokens for LBRACKET and RBRACKET as appropriate.
  //
  // We don't actually have an escape character inside layout.
  // The way to write a ] or [ is to use expression mode, like ["["].
  //
  // As a result, a layout literal is just any text (not containing
  // square brackets) between any square brackets. We have special
  // handling to ignore [* layout comments *], however.
#define ANY_BRACKET R"([\[\]])"
  static const RE2 layoutlit(
      ANY_BRACKET
      "(?:"
      R"([^\[\]])" "*"
      ")*"
      ANY_BRACKET);

  static const std::unordered_map<std::string, TokenType> keywords = {
    {"let", LET},
    {"do", DO},
    {"end", END},
    {"in", IN},
    {"fn", FN},
    {"of", OF},
    {"datatype", DATATYPE},
    {"case", CASE},
    {"fun", FUN},
    {"val", VAL},
    {"true", TRUE},
    {"false", FALSE},
    {"if", IF},
    {"then", THEN},
    {"else", ELSE},
    {"andalso", ANDALSO},
    {"orelse", ORELSE},
    {"andthen", ANDTHEN},
    {"otherwise", OTHERWISE},

    {"as", AS},
    {"and", AND},
    {"import", IMPORT},
    {"fail", FAIL},
    {"object", OBJECT},
    {"with", WITH},
    {"without", WITHOUT},

    // Symbolic
    {"=>", DARROW},
    {"->", ARROW},
    {"_", UNDERSCORE},
    {"=", EQUALS},
    {"|", BAR},
    {"*", TIMES},
    {":", COLON},
    {"#", HASH},
    {"/", SLASH},
  };

  re2::StringPiece input(input_string);

  // Get the current offset of the stringpiece.
  auto Pos = [&input_string, &input]() {
      CHECK(input.data() >= input_string.data());
      CHECK(input.data() <= input_string.data() + input_string.size());
      return input.data() - input_string.data();
    };

  std::vector<Token> ret;

  while (!input.empty()) {
    const size_t start = Pos();
    std::string match;
    if (RE2::Consume(&input, whitespace)) {
      // No tokens.
      // printf("Saw whitespace at %zu for %zu\n", start, Pos() - start);
    } else if (RE2::Consume(&input, start_comment)) {
      // No tokens.
      int depth = 1;
      for (;;) {
        // Must be enough room for the closing comment.
        if (input.size() < 2) {
          if (error != nullptr) {
            *error = "Unterminated comment";
          }
          return std::nullopt;
        }

        if (input[0] == '(' &&
            input[1] == '*') {
          depth++;
          input.remove_prefix(2);
        } else if (input[0] == '*' &&
                   input[1] == ')') {
          depth--;
          input.remove_prefix(2);
          if (depth == 0) {
            // Outermost comment ends; back to regular lexing.
            break;
          }
        } else {
          input.remove_prefix(1);
        }
      }

    } else if (RE2::Consume(&input, explicit_numeric_lit)) {
      // Must come before digits so that we don't parse the
      // 0 prefix as digits.
      ret.emplace_back(NUMERIC_LIT, start, Pos() - start);
    } else if (RE2::Consume(&input, float_lit)) {
      ret.emplace_back(FLOAT_LIT, start, Pos() - start);
    } else if (RE2::Consume(&input, negative_decimal)) {
      // Must come after float lit, so that we don't treat -999.0
      // as "-999" followed by ".0".
      ret.emplace_back(NUMERIC_LIT, start, Pos() - start);
    } else if (RE2::Consume(&input, digits)) {
      // Digits must come AFTER floats and prefixed stuff like 0x and
      // 0u, since otherwise we'd parse the leading 0 or integer part
      // as digits.
      ret.emplace_back(DIGITS, start, Pos() - start);
    } else if (RE2::Consume(&input, ident, &match)) {
      const auto kit = keywords.find(match);
      if (kit != keywords.end()) {
        ret.emplace_back(kit->second, start, Pos() - start);
      } else {
        ret.emplace_back(ID, start, Pos() - start);
      }
    } else if (RE2::Consume(&input, strlit)) {
      ret.emplace_back(STR_LIT, start, Pos() - start);
    } else if (RE2::Consume(&input, layoutlit)) {
      const size_t len = Pos() - start;
      // Must match open and closing bracket, at least.
      CHECK(len >= 2);
      TokenType brack1 = INVALID, brack2 = INVALID;
      char c1 = input_string[start];
      char c2 = input_string[start + len - 1];
      switch (c1) {
      case '[': brack1 = LBRACKET; break;
      case ']': brack1 = RBRACKET; break;
      default: CHECK(false) << c1;
      }
      switch (c2) {
      case '[': brack2 = LBRACKET; break;
      case ']': brack2 = RBRACKET; break;
      default: CHECK(false) << c2;
      }

      ret.emplace_back(brack1, start, 1);
      ret.emplace_back(LAYOUT_LIT, start + 1, len - 2);
      ret.emplace_back(brack2, start + len - 1, 1);

      // Now, if we just lexed [one of these[
      //                    or ]one of these[
      // then optionally lex a trailing [* layout comment *].

      if (!input.empty() && input[0] == '*') {
        // the position of the *
        const size_t comment_start = Pos();
        input.remove_prefix(1);
        // The only thing that ends this is *].
        auto comment_len = input.find("*]");

        if (comment_len == re2::StringPiece::npos) {
          if (error != nullptr) {
            *error =
              StringPrintf("Unterminated [* layout comment *] "
                           "at offset %zu",
                           comment_start);
          }
          return std::nullopt;
        }

        // comment_len is to the beginning of *], but we want to
        // include the trailing *. We don't include the ] so that
        // it can be picked up as part of a LAYOUT_LIT on the
        // next iteration.
        ret.emplace_back(LAYOUT_COMMENT, comment_start, comment_len + 1);
        input.remove_prefix(comment_len + 1);
      }

    } else {

      char c = input[0];
      switch (c) {
        // Could use a table for this...
      case '(':
        ret.emplace_back(LPAREN, start, 1);
        input.remove_prefix(1);
        continue;
      case ')':
        ret.emplace_back(RPAREN, start, 1);
        input.remove_prefix(1);
        continue;
      case ',':
        ret.emplace_back(COMMA, start, 1);
        input.remove_prefix(1);
        continue;
      case '.':
        ret.emplace_back(PERIOD, start, 1);
        input.remove_prefix(1);
        continue;
      case '{':
        ret.emplace_back(LBRACE, start, 1);
        input.remove_prefix(1);
        continue;
      case '}':
        ret.emplace_back(RBRACE, start, 1);
        input.remove_prefix(1);
        continue;
      case ';':
        ret.emplace_back(SEMICOLON, start, 1);
        input.remove_prefix(1);
        continue;

      default: {
        const char *msg = "Unexpected character";
        switch (c) {
        case '\"': msg = "Invalid string literal starting with"; break;
        case '[':
        case ']': msg = "Invalid layout literal starting with"; break;
        default: break;
        }
        // XXX get line info
        if (error != nullptr) {
          *error =
            StringPrintf("%s '%c' (0x%02x) at offset %zu",
                         msg, c, c, start);
        }
        return std::nullopt;
      }
      }
    }
  }
  return {ret};
}

std::string Lexing::UnescapeStrLit(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (int i = 0; i < (int)s.size(); i++) {
    const char c = s[i];
    if (c == '\\') {
      CHECK(i < (int)s.size() - 1) << "Bug: Trailing escape "
        "character in string literal.";
      i++;
      const char d = s[i];
      switch (d) {
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case '\\': out.push_back('\\'); break;
      case '\"': out.push_back('\"'); break;
      default:
        // TODO: Implement \x and \u{1234} stuff.
        CHECK(false) << "Unimplemented or illegal escape "
                     << StringPrintf("\\%c", d)
                     << " in string literal.";
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string Lexing::UnescapeLayoutLit(const std::string &s) {
  // Nothing to do: There are no escaped characters in layout.
  return s;
}

}  // namespace el

