
#include "parsing.h"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/stringprintf.h"
#include "formula.h"
#include "parser-combinators.h"
#include "re2/re2.h"
#include "util.h"

std::string TokenTypeString(TokenType t) {
  switch (t) {
  case COMMA: return "COMMA";
  case EQUALS: return "EQUALS";
  case LESS: return "LESS";
  case GREATER: return "GREATER";
  case PLUS: return "PLUS";
  case MINUS: return "MINUS";
  case HASH: return "HASH";
  case COLON: return "COLON";
  case PERIOD: return "PERIOD";
  case LPAREN: return "LPAREN";
  case RPAREN: return "RPAREN";
  case LBRACE: return "LBRACE";
  case RBRACE: return "RBRACE";
  case LSQUARE: return "LSQUARE";
  case RSQUARE: return "RSQUARE";
  case ARROW: return "ARROW";

  case NUMBER: return "NUMBER";
  case SYMBOL: return "SYMBOL";
  case COMMENT: return "COMMENT";
  default: return "???";
  }
}

std::vector<Token> Tokenize(int line_num,
                            const std::string &input_string) {
  static const RE2 whitespace("[ \r\n\t]+");

  // Numeric literals of various sorts.
  // There are $hex, %binary, and decimal.
  static const RE2 hex_numeric_lit("[$]([0-9a-fA-F]+)");
  static const RE2 binary_numeric_lit("[%]([01]+)");
  static const RE2 decimal_numeric_lit("(-?[0-9]+)");

  static const RE2 ident("([A-Za-z_][A-Za-z_0-9]*)");

  re2::StringPiece input(input_string);

  std::vector<Token> ret;
  auto AddSimpleToken = [&ret, &input](TokenType t) {
      ret.push_back(SimpleToken(t));
      input.remove_prefix(1);
    };

  while (!input.empty()) {
    if (input.starts_with("->")) {
      ret.push_back(SimpleToken(ARROW));
      input.remove_prefix(2);
      continue;
    }

    std::string match;
    switch (input[0]) {
    case '+': AddSimpleToken(PLUS); continue;
    case '-': AddSimpleToken(MINUS); continue;
    case '#': AddSimpleToken(HASH); continue;
    case '<': AddSimpleToken(LESS); continue;
    case '>': AddSimpleToken(GREATER); continue;
    case '=': AddSimpleToken(EQUALS); continue;
    case '.': AddSimpleToken(PERIOD); continue;
    case ':': AddSimpleToken(COLON); continue;
    case ',': AddSimpleToken(COMMA); continue;
    case '(': AddSimpleToken(LPAREN); continue;
    case ')': AddSimpleToken(RPAREN); continue;
    case '{': AddSimpleToken(LBRACE); continue;
    case '}': AddSimpleToken(RBRACE); continue;
    case '[': AddSimpleToken(LSQUARE); continue;
    case ']': AddSimpleToken(RSQUARE); continue;

    case ';':
      // Read to end of line, and then we are done.
      ret.push_back(Token{.type = COMMENT, .str = (std::string)input});
      input = "";
      continue;
    default:
      break;
    }

    // Otherwise, try regex-based tokens.
    if (RE2::Consume(&input, whitespace)) {
      // No tokens.
    } else if (int64_t num;
               RE2::Consume(&input, decimal_numeric_lit, &num)) {
      ret.push_back(Token{.type = NUMBER, .num = num});
    } else if (std::string hex;
               RE2::Consume(&input, hex_numeric_lit, &hex)) {
      ret.push_back(Token{
          .type = NUMBER,
          .num = strtol(hex.c_str(), nullptr, 16),
        });
    } else if (std::string bin;
               RE2::Consume(&input, binary_numeric_lit, &bin)) {
      ret.push_back(Token{
          .type = NUMBER,
          .num = strtol(bin.c_str(), nullptr, 2),
        });
    } else if (std::string str;
               RE2::Consume(&input, ident, &str)) {

      if (str == "in") {
        AddSimpleToken(IN);
        continue;
      } else {
        ret.push_back(Token{
            .type = SYMBOL,
            .str = str,
          });
      }
    } else {
      LOG(FATAL) << "Could not parse line " << line_num
                 << " (tokenization):\n"
                 << input_string << "\n"
                 << "Looking at: [" << input << "]\n";
    }
  }
  return ret;
}

std::string ExpString(const Exp *e) {
  if (e == nullptr) return "??NULL??";
  switch (e->type) {
  case ExpType::LABEL:
    return StringPrintf("'%s'", e->label.c_str());
  case ExpType::NUMBER:
    return StringPrintf("%lld", e->number);
  case ExpType::HIGH_BYTE:
    return StringPrintf(">%s", ExpString(e->a.get()).c_str());
  case ExpType::LOW_BYTE:
    return StringPrintf("<%s", ExpString(e->a.get()).c_str());
  case ExpType::PLUS:
    return StringPrintf("(%s + %s)",
                        ExpString(e->a.get()).c_str(),
                        ExpString(e->b.get()).c_str());
  case ExpType::MINUS:
    return StringPrintf("(%s - %s)",
                        ExpString(e->a.get()).c_str(),
                        ExpString(e->b.get()).c_str());
  default:
    return "??UNKNOWN??";
  }
}

template<TokenType t>
struct IsToken {
  using token_type = Token;
  using out_type = Token;
  constexpr IsToken() {}
  Parsed<Token> operator()(TokenSpan<Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None();
    if (toks[0].type == t) return Parsed(toks[0], 1);
    else return Parsed<Token>::None();
  }
};

// Like IsIdentifier("ram"). Note that it retains a pointer,
// so it should typically be used with a string literal.
namespace {
struct IsIdentifier {
  using token_type = Token;
  using out_type = Token;
  constexpr IsIdentifier(const char *s) : s(s) {}
  Parsed<Token> operator()(TokenSpan<Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None();
    if (toks[0].type == SYMBOL &&
        toks[0].str == s) return Parsed(toks[0], 1);
    else return Parsed<Token>::None();
  }
  const char *s = nullptr;
};
}

static std::shared_ptr<Form> ParseForm(
    const std::vector<Token> &tokens,
    const std::function<std::string()> &Error) {

  using FixityElt = FixityItem<std::shared_ptr<Form>>;
  const auto ResolveFormFixity = [&](const std::vector<FixityElt> &elts) ->
    std::optional<std::shared_ptr<Form>> {
    // No legal adjacency case.
    return ResolveFixity<std::shared_ptr<Form>>(elts, nullptr);
  };

  const FixityElt InElt = {
    .fixity = Fixity::Infix,
    .assoc = Associativity::Non,
    .precedence = 9,
    .item = nullptr,
    .unop = nullptr,
    .binop = [&](std::shared_ptr<Form> a, std::shared_ptr<Form> b) ->
    std::shared_ptr<Form> {
      return std::make_shared<Form>(BinForm{
          .op = Binop::IN,
          .lhs = std::move(a),
          .rhs = std::move(b),
        });
    },
  };

  const auto FormExp =
    Fix<Token, std::shared_ptr<Form>>([&](const auto &Self) {
        auto Number =
          IsToken<NUMBER>() >[&](Token t) {
              return std::make_shared<Form>(IntForm{
                  .value = t.num,
                });
            };

        auto Var =
          IsToken<SYMBOL>() >[&](Token t) {
              return std::make_shared<Form>(VarForm{
                  .name = t.str,
                });
            };

        auto Set =
          (IsToken<LBRACE>() >>
           Separate0(Self, IsToken<COMMA>()) <<
           IsToken<RBRACE>())
          >[&](const std::vector<std::shared_ptr<Form>> &fs) {
              return std::make_shared<Form>(NaryForm{
                  .op = Naryop::SET,
                  .v = fs,
                });
            };

        auto ReadRam =
          (IsIdentifier("ram") >>
           IsToken<LSQUARE>() >>
           Self <<
           IsToken<RSQUARE>())
          >[&](const std::shared_ptr<Form> &form) {
              return std::make_shared<Form>(UnForm{
                  .op = Unop::RAM,
                  .arg = form,
                });
            };

        auto AtomicExp = Number || Var || Set;

        auto FixityElement =
          (IsToken<IN>() >> Succeed<Token, FixityElt>(InElt)) ||
          (AtomicExp >[&](std::shared_ptr<Form> e) {
              FixityElt item;
              item.fixity = Fixity::Atom;
              item.item = std::move(e);
              return item;
            });

        return +FixityElement /= ResolveFormFixity;
      });


  auto Program = FormExp << End<Token>();
  auto parseopt = Program(TokenSpan<Token>(tokens));
  CHECK(parseopt.HasValue()) << "Expected formula."
                             << Error();
  return parseopt.Value();
}

Line ParseLine(const std::vector<Token> &tokens,
               const std::function<std::string()> &Error) {

  using FixityElt = FixityItem<std::shared_ptr<Exp>>;
  const auto ResolveExpFixity = [&](const std::vector<FixityElt> &elts) ->
    std::optional<std::shared_ptr<Exp>> {
    // No legal adjacency case.
    return ResolveFixity<std::shared_ptr<Exp>>(elts, nullptr);
  };

  const FixityElt PlusElt = {
    .fixity = Fixity::Infix,
    .assoc = Associativity::Left,
    .precedence = 9,
    .item = nullptr,
    .unop = nullptr,
    .binop = [&](std::shared_ptr<Exp> a, std::shared_ptr<Exp> b) ->
    std::shared_ptr<Exp> {
      return std::make_shared<Exp>(Exp{
          .type = ExpType::PLUS,
          .a = std::move(a),
          .b = std::move(b),
        });
    },
  };

  const FixityElt MinusElt = {
    .fixity = Fixity::Infix,
    .assoc = Associativity::Left,
    .precedence = 9,
    .item = nullptr,
    .unop = nullptr,
    .binop = [&](std::shared_ptr<Exp> a, std::shared_ptr<Exp> b) ->
    std::shared_ptr<Exp> {
      return std::make_shared<Exp>(Exp{
          .type = ExpType::MINUS,
          .a = std::move(a),
          .b = std::move(b),
        });
    },
  };

  const FixityElt LessElt = {
    .fixity = Fixity::Prefix,
    .assoc = Associativity::Non,
    .precedence = 9,
    .item = nullptr,
    .unop = [&](std::shared_ptr<Exp> a) -> std::shared_ptr<Exp> {
      return std::make_shared<Exp>(Exp{
          .type = ExpType::LOW_BYTE,
          .a = std::move(a),
        });
    },
    .binop = nullptr,
  };

  const FixityElt GreaterElt = {
    .fixity = Fixity::Prefix,
    .assoc = Associativity::Non,
    .precedence = 9,
    .item = nullptr,
    .unop = [&](std::shared_ptr<Exp> a) -> std::shared_ptr<Exp> {
      return std::make_shared<Exp>(Exp{
          .type = ExpType::HIGH_BYTE,
          .a = std::move(a),
        });
    },
    .binop = nullptr,
  };

  const auto Expression =
    Fix<Token, std::shared_ptr<Exp>>([&](const auto &Self) {
        auto Number =
          IsToken<NUMBER>() >[&](Token t) {
              return std::make_shared<Exp>(Exp{
                  .type = ExpType::NUMBER,
                  .number = t.num,
                });
            };

        auto Symbol =
          IsToken<SYMBOL>() >[&](Token t) {
              return std::make_shared<Exp>(Exp{
                  .type = ExpType::LABEL,
                  .label = t.str,
                });
            };

        auto AtomicExp = Number || Symbol;

        auto FixityElement =
          (IsToken<PLUS>() >> Succeed<Token, FixityElt>(PlusElt)) ||
          (IsToken<MINUS>() >> Succeed<Token, FixityElt>(MinusElt)) ||
          (IsToken<LESS>() >> Succeed<Token, FixityElt>(LessElt)) ||
          (IsToken<GREATER>() >> Succeed<Token, FixityElt>(GreaterElt)) ||
          (AtomicExp >[&](std::shared_ptr<Exp> e) {
              FixityElt item;
              item.fixity = Fixity::Atom;
              item.item = std::move(e);
              return item;
            });

        return +FixityElement /= ResolveExpFixity;
      });

  // For detecting a specific register name. Case insensitive, but argument
  // symbol should be lowercase.
  auto IsSymbol = [&](const char *s) {
      return IsToken<SYMBOL>() /= [s](const Token t) -> std::optional<char> {
        return Util::lcase(t.str) == s ? std::make_optional('!') : std::nullopt;
      };
    };

  auto AddressingExp =
    ((IsToken<LPAREN>() >>
      Expression << IsToken<COMMA>() << IsSymbol("x") <<
      IsToken<RPAREN>()) >[&](auto e) {
        return Addressing(Addressing::INDIRECT_X, e);
      }) ||

    ((IsToken<LPAREN>() >> Expression << IsToken<RPAREN>() <<
      IsToken<COMMA>() << IsSymbol("y")) >[&](auto e) {
        return Addressing(Addressing::INDIRECT_Y, e);
      }) ||

    ((IsToken<LPAREN>() >> Expression << IsToken<RPAREN>()) >[&](auto e) {
        return Addressing(Addressing::INDIRECT, e);
      }) ||
    ((IsToken<HASH>() >> Expression) >[&](auto e) {
        return Addressing(Addressing::IMMEDIATE, e);
      }) ||
    (Expression << IsToken<COMMA>() << IsSymbol("x") >[&](auto e) {
        return Addressing(Addressing::ADDR_X, e);
      }) ||
    (Expression << IsToken<COMMA>() << IsSymbol("y") >[&](auto e) {
        return Addressing(Addressing::ADDR_Y, e);
      }) ||
    (Expression >[&](auto e) {
        return Addressing(Addressing::ADDR, e);
      }) ||

    // This assembler syntax lets you just write "ror" for "ror a".
    (Opt(IsSymbol("a")) >[&](auto) {
        return Addressing(Addressing::ACCUMULATOR);
      }) ||

    Fail<Token, Addressing>();

  auto GetAddressingMode = [&]() -> Addressing {
    auto Program = AddressingExp << End<Token>();

    auto parseopt = Program(TokenSpan<Token>(tokens.data() + 1,
                                             tokens.size() - 1));
    CHECK(parseopt.HasValue()) << "Expected addressing mode."
                               << Error();
    return parseopt.Value();
  };


  auto GetCommaSeparatedExpressions = [&](const std::vector<Token> &tokens) ->
    std::vector<std::shared_ptr<Exp>> {
    auto Program = Separate0(Expression, IsToken<COMMA>()) << End<Token>();
    auto parseopt = Program(TokenSpan<Token>(tokens));
    CHECK(parseopt.HasValue()) << "Expected comma separated expressions."
                               << Error();
    return parseopt.Value();
  };

  auto RestTokens = [&tokens, &Error](int skip) -> std::vector<Token> {
    CHECK(skip <= tokens.size()) << skip << " / " << tokens.size()
                                 << Error();
    std::vector<Token> ret;
    ret.reserve(tokens.size() - skip);
    for (int i = skip; i < tokens.size(); i++) {
      ret.push_back(tokens[i]);
    }
    return ret;
  };

  if (tokens.empty()) return Line{.type = Line::Type::NOTHING};

  // parse directives...
  if (tokens[0].type == PERIOD) {
    CHECK(tokens.size() > 1 &&
          tokens[1].type == SYMBOL) << "Expected directive after period."
                                    << Error();

    const std::string &dir = tokens[1].str;
    if (dir == "index" || dir == "mem" || dir == "org") {
      CHECK(tokens.size() == 3 && tokens[2].type == NUMBER) <<
        "index, mem, and org take a number." << Error();
      if (dir == "index") {
        return Line{
          .type = Line::Type::DIRECTIVE_INDEX,
          .num = (int)tokens[2].num,
        };
      } else if (dir == "mem") {
        return Line{
          .type = Line::Type::DIRECTIVE_MEM,
          .num = (int)tokens[2].num,
        };
      } else if (dir == "org") {
        return Line{
          .type = Line::Type::DIRECTIVE_ORG,
          .num = (int)tokens[2].num,
        };
      }
    } else if (dir == "db" || dir == "dw") {
      Line ret;
      ret.type = (dir == "db") ?
        Line::Type::DIRECTIVE_DB : Line::Type::DIRECTIVE_DW;
      ret.exps = GetCommaSeparatedExpressions(RestTokens(2));
      return ret;
    } else if (dir == "always") {
      Line ret;
      ret.type = Line::Type::DIRECTIVE_ALWAYS;
      ret.formula = ParseForm(RestTokens(2), Error);
      return ret;
    }

    LOG(FATAL) << "Unknown directive: " << dir << Error();
  }

  if (tokens.size() > 2 &&
      tokens[0].type == SYMBOL &&
      tokens[1].type == EQUALS) {
    Line ret;
    ret.type = Line::Type::CONSTANT_DECL;
    ret.symbol = tokens[0].str;
    ret.exps = GetCommaSeparatedExpressions(RestTokens(2));
    CHECK(ret.exps.size() == 1) << "Symbolic constant definition "
      "just takes one expression." << Error();
    return ret;
  }


  CHECK(!tokens.empty() && tokens[0].type == SYMBOL) << "Expected "
    "instruction mnemonic." << Error();

  Line inst;
  inst.type = Line::Type::INSTRUCTION;
  inst.symbol = tokens[0].str;
  inst.addressing = GetAddressingMode();
  return inst;
}

void StripComments(std::vector<Token> *tokens,
                   const std::function<std::string()> &Error) {
  // Any line can have a trailing comment. Pull that off to start.
  std::string trailing_comment;
  if (!tokens->empty() && tokens->back().type == COMMENT) {
    trailing_comment = tokens->back().str;
    tokens->pop_back();
  }

  // Now there should be no other comments.
  for (const Token &token : *tokens) {
    CHECK(token.type != COMMENT) << "Can't have multiple comments on "
      "a line." << Error();
  }
}

std::optional<std::string> ConsumeLabel(
    std::vector<Token> *tokens,
    const std::function<std::string()> &Error) {

  if (tokens->size() >= 2 &&
      (*tokens)[0].type == SYMBOL &&
      (*tokens)[1].type == COLON) {
    std::string symbol = std::move((*tokens)[0].str);

    // Remove the label.
    tokens->erase(tokens->begin(), tokens->begin() + 2);
    return {symbol};
  }

  return std::nullopt;
}
