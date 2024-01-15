
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
#define ANY_BRACKET R"([\[\]])"
  static const RE2 layoutlit(
      ANY_BRACKET
      "(?:"
      R"([^\[\]\\])" "|"
      R"(\\.)"
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
      CHECK(i < (int)s.size() - 1) << "Trailing escape character "
        "in string literal.";
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


  /*
  const auto LayoutExpr = [&](const auto &Expr) {
      return
        IsToken<LBRACKET>() >>
    IsToken<
    << IsToken<RBRACKET>()
    };
  */

  // XXX probably will need a FixN for exp/dec...
  // XXX or other types...
  const auto Expr =
    Fix<Token, const Exp *>([&](const auto &Self) {
        return
          IntExpr ||
          VarExpr ||
          StrLitExpr ||
          TupleExpr(Self) ||
          // Just here for convenience of writing a || b || ...
          Fail<Token, const Exp *>();
      });

  #if 0
  static constexpr nterm<const Exp *> exp("exp");
  static constexpr nterm<std::deque<const Exp *>>
    comma_separated_exp("comma_separated_exp");
  static constexpr nterm<std::deque<const Exp *>>
    comma_continued_exp("comma_continued_exp");

  static constexpr nterm<const Layout *> layout("layout");
  static constexpr nterm<const Layout *> after_layout("after_layout");

  static constexpr char digits_pattern[] = "[1-9][0-9]*";
  static constexpr regex_term<digits_pattern> digits("digits");

  static constexpr char id_pattern[] = "[A-Za-z_][A-Za-z0-9_]*";
  static constexpr regex_term<id_pattern> id("id");

  static constexpr parser p(
      // This is the grammar root. We're parsing an expression.
      exp,
      // All the terminal symbols. Characters and strings implicitly
      // stand for themselves.
      terms(',', '(', ')', leading_layout, trailing_layout,
            // must be lower precedence than the layout tokens
            '[', ']',
            digits, id, strlit),
      nterms(exp, comma_separated_exp, comma_continued_exp,
             layout, after_layout),

      rules(
          comma_continued_exp() >= []() {
              return std::deque<const Exp *>({});
            },
          comma_continued_exp(',', exp, comma_continued_exp) >=
          [](auto, const Exp *e, std::deque<const Exp *> &&v) {
            v.push_front(e);
            return std::move(v);
          },

          comma_separated_exp() >= []() {
              return std::deque<const Exp *>({});
            },
          comma_separated_exp(exp, comma_continued_exp) >=
          [](const Exp *e, std::deque<const Exp *> &&v) {
              v.push_front(e);
              return std::move(v);
            },

          exp(digits) >>= [](auto &ctx, std::string_view d) {
              return ctx->Int(std::stoll(std::string(d)));
            },
          exp(id) >>= [](auto &ctx, std::string_view d) {
              return ctx->Var(std::string(d));
            },
          exp(strlit) >>= [](auto &ctx,
                             std::string_view s) {
              // XXX interpret escapes here?
              CHECK(s.size() >= 2);
              return ctx->Str((std::string)s.substr(1, s.size() - 2));
            },

          exp('(', comma_separated_exp, ')')
          >>= [](auto &ctx, auto _l,
                 std::deque<const Exp *> &&d,
                 auto _r) {
              std::vector<const Exp *> v(d.begin(), d.end());
              return ctx->Tuple(std::move(v));
            },

          // [layoutAFTER
          // where AFTER is defined below...
          exp(leading_layout, after_layout)
          >>= [](auto &ctx, std::string_view d, const Layout *lay1) {
              // Skip leading [.
              const std::string s = (std::string)d.substr(1);
              printf("Leading layout: __%s__\n", s.c_str());
              const Layout *lay2 = ctx->TextLayout(std::move(s));
              return ctx->LayoutExp(ctx->JoinLayout({lay1, lay2}));
            },

          // AFTER can either just end the layout,
          after_layout(']') >>= [](auto &ctx, auto) {
              return ctx->TextLayout("");
            }

          #if 0
          ,
          // or enter another nested expression, and continue
          after_layout('[', exp, trailing_layout, after_layout)
          >>= [](auto &ctx, auto, const Exp *exp,
                 std::string_view d, const Layout *lay2) {
              // skip the trailing ]
              std::string s = (std::string)d.substr(1);
              return ctx->JoinLayout(
                  {ctx->ExpLayout(exp),
                   ctx->TextLayout(std::move(s)),
                   lay2});
            }
          #endif
     )
  );

  printf(ABGCOLOR(60, 60, 200, "==== GRAMMAR ====") "\n");
  p.write_diag_str(std::cout);
  printf(ABGCOLOR(60, 60, 200, "==== END ====") "\n");

  auto Parse = [&](std::string s) {
      printf("Parsing %s...\n", s.c_str());
      auto res = p.context_parse(pool,
                                 parse_options{}.set_skip_whitespace(true).
                                 set_verbose(),
                                 string_buffer(std::move(s)),
                                 std::cerr);
      CHECK(res.has_value());
      printf("  Got: %s\n", ExpString(res.value()).c_str());
      return res.value();
    };

  return Parse(input);
#endif

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


#if 0
  {
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
    const Layout *lay =e->children[1]->layout;
    CHECK(lay->type == LayoutType::TEXT);
    CHECK(lay->str == "layout");
    CHECK(e->children[2]->integer == 888);
  }
#endif

}


// XXX to test
int main(int argc, char **argv) {
  Test();

  printf("OK");
  return 0;
}
