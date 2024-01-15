
#include <string>
#include <vector>
#include <deque>
#include <cstdint>

#include "parser-combinators.h"
#include "ast.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "re2/re2.h"

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

  DIGITS,

  // Keywords.
  FN,

  // Identifier.
  ID,

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

static const char *TokenTypeString(TokenType tok) {
  switch (tok) {
  case LPAREN: return "LPAREN";
  case RPAREN: return "RPAREN";
  case LBRACKET: return "LBRACKET";
  case RBRACKET: return "RBRACKET";
  case COMMA: return "COMMA";
  case PERIOD: return "PERIOD";

  case DIGITS: return "DIGITS";

  // Keywords.
  case FN: return "FN";

  // Identifier.
  case ID: return "ID";

  case LAYOUT_LIT: return "LAYOUT_LIT";
  case STR_LIT: return "STR_LIT";

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

// Returns the source string (with colors) and the sequence of
// tokens (with colors).
static std::pair<std::string, std::string> ColorTokens(
    const std::string &input_string,
    const std::vector<Token> &tokens) {
  std::string source, ctokens;
  int color_idx = 0;
  size_t input_pos = 0;
  for (int t = 0; t < (int)tokens.size(); t++) {
    const Token &tok = tokens[t];
    /*
    printf("pos %zu. tok %d from %zu for %zu\n", input_pos,
           t, tok.start, tok.length);
    */
    CHECK(input_pos <= tok.start);
    CHECK(tok.start < input_string.size());
    CHECK(tok.start + tok.length <= input_string.size());
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

#define LAYOUT_CHARS

// Lexing.
static std::vector<Token> Lex(const std::string &input_string) {
  static const RE2 whitespace("[ \r\n\t]+");
  static const RE2 digits("[0-9]+");
  // TODO: Allow UTF-8
  static const RE2 ident("([A-Za-z][-'_A-Za-z0-9]*)");
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

  // Regex for the interior of a [layout literal].
  // When we use antiquote for a nested expression, like
  // [aaa[exp]bbb], aaa and bbb are lexed as separate tokens.
  // Unlike string literals, which include their quotation
  // marks in the token, when we lex a layout literal we
  // also output tokens for LBRACKET and RBRACKET as appropriate.
  //
  // We don't actually have an escape character inside layout.
  // The way to write a ] or [ is to use expression mode, like ["["].
#define ANY_BRACKET R"([\[\]])"
  static const RE2 layoutlit(
      ANY_BRACKET
      "(?:"
      R"([^\[\]])" "*"
      ")*"
      ANY_BRACKET);

  static const std::unordered_map<std::string, TokenType> keywords = {
    {"fn", FN},
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
      /* no tokens. */
      // printf("Saw whitespace at %zu for %zu\n", start, Pos() - start);
    } else if (RE2::Consume(&input, digits)) {
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

      default: {
        const char *msg = "Unexpected character";
        switch (c) {
        case '\"': msg = "Invalid string literal starting with"; break;
        case '[':
        case ']': msg = "Invalid layout literal starting with"; break;
        default: break;
        }
        // XXX get line info
        CHECK(false) <<
          StringPrintf("%s '%c' (0x%02x) at offset %zu\n",
                       msg, c, c, start) << "\nIn input: "
                     << input_string;
      }
      }
    }
  }
  return ret;
}

static std::string UnescapeStrLit(const std::string &s) {
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

// Nothing to do: There are no escaped characters in layout.
static inline std::string UnescapeLayoutLit(const std::string &s) {
  return s;
}

template<TokenType t>
struct IsToken {
  using token_type = Token;
  using out_type = Token;
  constexpr IsToken() {}
  constexpr Parsed<Token> operator()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None;
    if (toks[0].type == t) return Parsed(toks[0], 1);
    else return Parsed<Token>::None;
  }
};


static const Exp *Parse(AstPool *pool, const std::string &input) {

  std::vector<Token> tokens = Lex(input);
  const auto &[source, ctokens] = ColorTokens(input, tokens);
  printf("%s\n%s\n", source.c_str(), ctokens.c_str());

  auto TokenStr = [&input](Token t) {
      CHECK(t.start <= input.size());
      CHECK(t.start + t.length <= input.size());
      return std::string(input.substr(t.start, t.length));
    };

  // TODO: Support other integer literals.
  const auto Int = IsToken<DIGITS>() >[&](Token t) {
      std::string s = TokenStr(t);
      int64_t i = std::stoll(s);
      CHECK(StringPrintf("%lld", i) == s) << "Invalid integer "
        "literal " << s;
      return i;
    };

  const auto Id = IsToken<ID>() >[&](Token t) { return TokenStr(t); };
  const auto StrLit = IsToken<STR_LIT>() >[&](Token t) {
      // Remove leading and trailing double quotes. Process escapes.
      std::string s = TokenStr(t);
      CHECK(s.size() >= 2) << "Bug: The double quotes are included "
        "in the token.";
      return UnescapeStrLit(s.substr(1, s.size() - 2));
    };

  const auto LayoutLit = IsToken<LAYOUT_LIT>() >[&](Token t) {
      return UnescapeLayoutLit(TokenStr(t));
    };

  const auto IntExpr = Int >[&](int64_t i) { return pool->Int(i); };
  const auto VarExpr = Id >[&](const std::string &s) {
      return pool->Var(s);
    };
  const auto StrLitExpr = StrLit >[&](const std::string &s) {
      return pool->Str(s);
    };

  // Either (), or (e) or (e1, e2, ...).
  const auto TupleExpr = [&](const auto &Expr) {
      return ((IsToken<LPAREN>() >>
               Separate0(Expr, IsToken<COMMA>()) <<
               IsToken<RPAREN>())
              >[&](const std::vector<const Exp *> &es) {
                  if (es.size() == 1) {
                    // Then this is just a parenthesized expression.
                    return es[0];
                  } else {
                    return pool->Tuple(es);
                  }
                });
    };


  const auto LayoutExpr = [&](const auto &Expr) {
      const auto Lay =
        Fix<Token, const Layout *>([&](const auto &Self) {
            return (LayoutLit &&
              *((IsToken<LBRACKET>() >> Expr << IsToken<RBRACKET>()) &&
                LayoutLit))
              >[&](const auto &p) {
                  const auto &[l1, v] = p;
                  const Layout *x1 = pool->TextLayout(l1);
                  if (v.empty()) {
                    // No need for a join node.
                    return x1;
                  } else {
                    std::vector<const Layout *> joinme;
                    joinme.reserve(1 + 2 * v.size());
                    joinme.push_back(x1);
                    for (const auto &[e, t] : v) {
                      joinme.push_back(pool->ExpLayout(e));
                      joinme.push_back(pool->TextLayout(t));
                    }
                    return pool->JoinLayout(std::move(joinme));
                  }
                };
          });

      return (IsToken<LBRACKET>() >> Lay << IsToken<RBRACKET>())
          >[&](const Layout *lay) {
              return pool->LayoutExp(lay);
            };
    };

  // XXX probably will need a FixN for exp/dec...
  // XXX or other types...
  const auto Expr =
    Fix<Token, const Exp *>([&](const auto &Self) {
        return
          IntExpr ||
          VarExpr ||
          StrLitExpr ||
          TupleExpr(Self) ||
          LayoutExpr(Self) ||
          // Just here for convenience of writing a || b || ...
          Fail<Token, const Exp *>();
      });

  auto Program = Expr << End<Token>();

  auto po = Program(std::span<Token>(tokens.data(), tokens.size()));
  CHECK(po.HasValue()) << "Could not parse program.";
  return po.Value();
}

static void Test() {
  AstPool pool;

  #if 0
  Parse(&pool,
        "the   cat fn() went to 1234 the \"string\" store\n"
        "where he \"\\\\slashed\\n\" t-i-r-e-s\n"
        "Here is a [nested [123] expression].\n"
        );
#endif


  {
    const Exp *e = Parse(&pool, "15232");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 15232);
  }

  {
    const Exp *e = Parse(&pool, "var");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::VAR);
    CHECK(e->str == "var");
  }

  {
    const Exp *e = Parse(&pool, " \"a string\" ");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "a string");
  }

  {
    const Exp *e = Parse(&pool, R"( "now:\nwith \\ \"escapes\"" )");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::STRING);
    CHECK(e->str == "now:\nwith \\ \"escapes\"");
  }

  {
    const Exp *e = Parse(&pool, "(123)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::INTEGER);
    CHECK(e->integer == 123);
  }

  {
    const Exp *e = Parse(&pool, "()");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 0);
  }

  {
    const Exp *e = Parse(&pool, "(var,123,\"yeah\")");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "var");
    CHECK(e->children[1]->integer == 123);
    CHECK(e->children[2]->str == "yeah");
  }

  {
    const Exp *e = Parse(&pool, "( xar , (333 ,777 ), 888)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "xar");
    CHECK(e->children[1]->type == ExpType::TUPLE);
    CHECK(e->children[1]->children.size() == 2);
    CHECK(e->children[1]->children[0]->integer == 333);
    CHECK(e->children[1]->children[1]->integer == 777);
    CHECK(e->children[2]->integer == 888);
  }


  {
    // Note that this is three tokens: an empty LAYOUT_LIT is
    // tokenized between the bracketse.
    const Exp *e = Parse(&pool, "[]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LAYOUT);
    CHECK(LayoutString(e->layout) == "");
  }

  {
    const Exp *e = Parse(&pool, "(xyz, [layout], 888)");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::TUPLE);
    CHECK(e->children.size() == 3);
    CHECK(e->children[0]->str == "xyz");
    CHECK(e->children[1]->type == ExpType::LAYOUT);
    const Layout *lay = e->children[1]->layout;
    CHECK(lay->type == LayoutType::TEXT);
    CHECK(lay->str == "layout");
    CHECK(e->children[2]->integer == 888);
  }

  {
    const Exp *e = Parse(&pool, "[layout[b]after]");
    CHECK(e != nullptr);
    CHECK(e->type == ExpType::LAYOUT);
    const std::vector<const Layout *> v =
      FlattenLayout(e->layout);
    CHECK(v.size() == 3) << v.size();
    CHECK(v[0]->type == LayoutType::TEXT);
    CHECK(v[0]->str == "layout");
    CHECK(v[1]->type == LayoutType::EXP);
    CHECK(v[1]->exp->type == ExpType::VAR);
    CHECK(v[1]->exp->str == "b");
    CHECK(v[2]->str == "after");
  }
}


// XXX to test
int main(int argc, char **argv) {
  Test();

  printf("OK");
  return 0;
}
