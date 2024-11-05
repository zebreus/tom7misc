// Simple 6502 assembler.
// Goal is to reproduce mario.nes (or really mario.prg) from mario.asm,
// following what x816 would do (that assembler seems to be so old that
// it needs DOSBox or similar to run!). I also want to generate the
// memory maps (.nl files) for the fceux debugger.

#include "ansi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "parser-combinators.h"
#include "re2/re2.h"
#include "util.h"

enum TokenType {
  // Singleton symbols
  COMMA,
  EQUALS,
  LESS,
  GREATER,
  PLUS,
  MINUS,
  HASH,
  COLON,
  PERIOD,
  LPAREN,
  RPAREN,

  // With payload.
  // In any format; parsed into the number it denotes.
  NUMBER,
  // any alphanumeric symbol,
  // including that part of directives, opcodes, register names,
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

static Token SimpleToken(TokenType t) {
  return Token{.type = t};
}

static std::string TokenTypeString(TokenType t) {
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
  case NUMBER: return "NUMBER";
  case SYMBOL: return "SYMBOL";
  case COMMENT: return "COMMENT";
  default: return "???";
  }
}

// This is a line-based syntax, so we tokenize and parse a line
// at a time.
static std::vector<Token> Tokenize(int line_num,
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
      ret.push_back(Token{
          .type = SYMBOL,
          .str = str,
        });
    } else {
      LOG(FATAL) << "Could not parse line " << line_num
                 << " (tokenization):\n"
                 << input_string << "\n"
                 << "Looking at: [" << input << "]\n";
    }
  }
  return ret;
}

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

static std::string ExpString(const Exp *e) {
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

// A delayed 16-bit quantity. These are written after we've finished
// the first pass and know the value of every label. We compute the
// 16-bit value and write it to the 16-bit dest_addr.
//
// For clarity: The address here is a machine address, not an offset
// into assembled bytes (but the offset can be computed using the
// origin).
struct Delayed16 {
  std::shared_ptr<Exp> exp;
  uint16_t dest_addr = 0;
};

// A delayed 8-bit displacement. These are measured from a
// source position. Note: It might be impossible to write the address
// if it doesn't fit in an int8.
// As above, both addresses here are machine addresses.
struct DelayedRel8 {
  std::string label;
  // The address that would have a displacement of 0.
  uint16_t base_addr = 0;
  // The place to write the 8-bit displacement.
  uint16_t dest_addr = 0;
};

struct Bank {
  // Creates an empty bank. The origin is required.
  Bank(int origin) : origin(origin) {}

  // The assembled data, which is expected to be loaded at the origin
  // address. The next instruction assembled goes at the end.
  std::vector<uint8_t> bytes;
  // The origin where the bytes will be loaded in memory, e.g. 0x8000.
  // This just affects what absolute value a label has.
  int origin = 0;

  // Named addresses. These are nominally uint16_t, but if -1,
  // then we do not know the address yet. These are machine addresses,
  // not offsets into bytes.
  std::unordered_map<std::string, int> labels;

  std::vector<Delayed16> delayed16;
  std::vector<DelayedRel8> delayedrel8;
};

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

static void Assemble(const std::string &filename) {
  std::vector<std::string> lines = Util::ReadFileToLines(filename);

  std::vector<Bank> banks;

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

        // Use prefix?
        #if 0
        auto ByteOfExp =
          ((IsToken<GREATER>() >> AtomicExp) >[&](auto child) {
              return std::make_shared<Exp>(Exp{
                  .type = ExpType::HIGH_BYTE,
                  .a = child,
                });
            }) ||
          ((IsToken<LESS>() >> AtomicExp) >[&](auto child) {
              return std::make_shared<Exp>(Exp{
                  .type = ExpType::LOW_BYTE,
                  .a = child,
                });
            });
        #endif


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


  for (int line_num = 0; line_num < (int)lines.size(); line_num++) {
    const std::string &line = lines[line_num];
    std::vector<Token> tokens_orig = Tokenize(line_num, line);

    auto Error = [line_num, &line, &tokens_orig]() {
        std::string toks;
        for (int t = 0; t < (int)tokens_orig.size(); t++) {
          if (t > 0) toks.push_back(' ');
          StringAppendF(&toks, "%s",
                        TokenTypeString(tokens_orig[t].type).c_str());
        }
        return StringPrintf("\nLine %d:\n%s\n%s", line_num,
                            line.c_str(), toks.c_str());
      };

    std::vector<Token> tokens = tokens_orig;

    // Any line can have a trailing comment. Pull that off to start.
    std::string trailing_comment;
    if (!tokens.empty() && tokens.back().type == COMMENT) {
      trailing_comment = tokens.back().str;
      tokens.pop_back();
    }

    // Now there should be no other comments.
    for (const Token &token : tokens) {
      CHECK(token.type != COMMENT) << "Can't have multiple comments on "
        "a line." << Error();
    }

    // Nothing to do on empty lines (or lines with just a comment).
    if (tokens.empty())
      continue;

    // The most common (and most complicated) thing is a series of
    // comma-separated expressions.
    auto GetCommaSeparatedExpressions =
      [&Expression, &tokens, &Error](int start_offset) {

        auto Program = Separate0(Expression, IsToken<COMMA>()) << End<Token>();

        auto parseopt = Program(TokenSpan<Token>(tokens.data() + start_offset,
                                                 tokens.size() - start_offset));
        CHECK(parseopt.HasValue()) << "Expected comma separated expressions."
                                   << Error();
        return parseopt.Value();

        #if 0
        // Null means we haven't seen anything yet.
        std::shared_ptr<Exp> current_exp;

        for (int i = start_offset; i < tokens.size(); i++) {
          switch (tokens[i].type) {
          case COMMA:
            CHECK(current_exp.get() != nullptr) << "Expected expression "
              "before comma!" << Error();
            exps.push_back(std::move(current_exp));
            current_exp.reset();
            break;

          case NUMBER:
            CHECK(current_exp.get() == nullptr) << "Unexpected number "
              "after expression." << Error();
            current_exp.reset(new Exp{
                .type = ExpType::NUMBER,
                .number = tokens[i].num,
              });
            break;

          default:
            LOG(FATAL) << "Unimplemented or unexpected token in expression."
                       << Error();
          }

        }

        if (current_exp.get() != nullptr) {
          exps.push_back(std::move(current_exp));
          current_exp.reset();
        }

        return exps;
        #endif
      };

    // Directives.
    if (tokens[0].type == PERIOD) {
      CHECK(tokens.size() > 1 &&
            tokens[1].type == SYMBOL) << "Expected directive after period."
                                      << Error();
      const std::string &dir = tokens[1].str;
      if (dir == "index") {
        CHECK(tokens.size() == 3 &&
              tokens[2].type == NUMBER &&
              tokens[2].num == 8) << "Only .index 8 is supported, and "
          "I also don't know what this means." << Error();
      } else if (dir == "mem") {
        CHECK(tokens.size() == 3 &&
              tokens[2].type == NUMBER &&
              tokens[2].num == 8) << "Only .mem 8 is supported, and "
          "I also don't know what this means." << Error();

      } else if (dir == "org") {
        CHECK(tokens.size() == 3 &&
              tokens[2].type == NUMBER &&
              tokens[2].num >= 0 &&
              tokens[2].num < 0x10000) << "Illegal .org directive."
                                       << Error();
        banks.emplace_back((int)tokens[2].num);
      } else if (dir == "db") {
        // Now read a series of expressions denoting bytes, and
        // write them.
        //
        // TODO: These should actually be delayed, since they could
        // refer to labels that have not been output yet. And they
        // should be expressions (e.g. <Address or Address+1).

        std::vector<std::shared_ptr<Exp>> exps =
          GetCommaSeparatedExpressions(2);

        for (const auto &e : exps) {
          printf("  " AGREY("%s") "\n", ExpString(e.get()).c_str());
        }

        LOG(FATAL) << "Unimplemented .db!" << Error();

      } else if (dir == "dw") {
        // A series of expressions denoting words, and
        // write them.

        std::vector<std::shared_ptr<Exp>> exps =
          GetCommaSeparatedExpressions(2);

        LOG(FATAL) << "Unimplemented .dw!" << Error();

      } else {

        LOG(FATAL) << "Unknown directive: " << dir << Error();
      }

    }

  }



}

int main(int argc, char **argv) {
  ANSI::Init();


  CHECK(argc == 3) << "./asm7.exe source.nes out.rom\n";

  Assemble(argv[1]);

  return 0;
}
