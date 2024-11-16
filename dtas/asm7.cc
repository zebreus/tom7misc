// Simple 6502 assembler.
//
// This reproduces mario.nes (or really mario.prg) byte-for-byte from
// mario.asm, following what x816 would do (that assembler seems to be
// so old that it needs DOSBox or similar to run!). I want to be able
// to be able to (for example) generate memory maps (.nl files) for
// the fceux debugger.
//
// It might have bugs outside of the instructions and techniques that
// mario.asm uses; no guarantees!

#include "ansi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "parser-combinators.h"
#include "randutil.h"
#include "re2/re2.h"
#include "util.h"

[[maybe_unused]]
static constexpr int VERBOSE = 1;

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

// A delayed expression. These are written after we've finished
// the first pass and know the value of every label. We compute the
// value and write it to the 16-bit dest_addr. Whether we write an
// 8-bit value or a 16-bit one needs to be determined by the context.
//
// For clarity: The address here is a machine address, not an offset
// into assembled bytes (but the offset can be computed using the
// origin).
struct Delayed {
  int line_num = 0;
  std::shared_ptr<Exp> exp;
  uint16_t dest_addr = 0;
};

struct Bank {
  // Creates an empty bank. The origin is required.
  Bank(int origin) : origin(origin) {}

  uint16_t NextAddress() const {
    return origin + (int)bytes.size();
  }

  void Write(uint16_t machine_addr, uint8_t v) {
    int offset = (int)machine_addr - origin;
    CHECK(offset >= 0 && offset < bytes.size()) <<
      StringPrintf("Bug: Write outside of bank bounds. Addr: %04x",
                   machine_addr);
    bytes[offset] = v;
  }

  // The assembled data, which is expected to be loaded at the origin
  // address. The next instruction assembled goes at the end.
  std::vector<uint8_t> bytes;
  // The origin where the bytes will be loaded in memory, e.g. 0x8000.
  // This just affects what absolute value a label has.
  int origin = 0;

  std::vector<Delayed> delayed_16;
  std::vector<Delayed> delayed_8;
  std::vector<Delayed> delayed_s8;

  std::map<uint16_t, std::string> debug_labels;
  // TODO: Comments too?
};

struct Assembly {
  // Symbolic constants include "Constant = $Value" and "Label:".
  // These are global to the assembly (and must be globally unique),
  // but an address might belong to a specific bank. The programmer
  // just has to manage this.

  // Symbols that we know the values of already.
  // The syntax does not distinguish between a declaration like
  //   BuzzyBeetle = $02
  // and
  //   BowserOrigXPos = $0366
  // even though the former is used as a constant byte, and the
  // second as a memory address. Additionally, a label
  //   WarpZoneWelcome:
  // is a machine address (not byte offset) that could be used
  // as a constant, or jumped to, etc. Basically, every symbol has
  // a value, but it is the use that tells us what it means (and
  // what the appropriate range of values is).
  std::unordered_map<std::string, int64_t> symbols;
  // Symbolic "constants" with delayed expressions. Note they may have
  // forward references within this set. (There could even be cycles,
  // which should be rejected later.) Note that the destination address
  // here is unused; the value is never written anywhere. It just gets
  // moved to the constants table.
  std::unordered_map<std::string, Delayed> delayed_symbols;

  // True if we already saw this symbol (even if we don't know its
  // value yet).
  bool HasSymbol(const std::string &sym) const {
    return symbols.contains(sym) || delayed_symbols.contains(sym);
  }

  // The ROM banks. Each one is a contiguous series of bytes that
  // will live at a given memory location (its origin).
  std::vector<Bank> banks;
};

// Evaluate to a number if possible. Note the number may be out of
// range for the target type.
static std::optional<int64_t> Evaluate(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  CHECK(exp != nullptr);
  switch (exp->type) {
  case ExpType::LABEL: {
    auto it = assembly->symbols.find(exp->label);
    if (it == assembly->symbols.end()) return std::nullopt;
    return {it->second};
  }

  case ExpType::NUMBER:
    return {exp->number};

  case ExpType::HIGH_BYTE: {
    if (auto io = Evaluate(assembly, exp->a.get(), Error)) {
      int64_t value = io.value();
      CHECK(value >= 0 && value <= 0xFFFF) << "In a > expression, the "
        "computed value is out of range: " << value << Error();
      return 0xFF & (value >> 8);
    } else {
      return std::nullopt;
    }
  }
  case ExpType::LOW_BYTE: {
    if (auto io = Evaluate(assembly, exp->a.get(), Error)) {
      int64_t value = io.value();
      CHECK(value >= 0 && value <= 0xFFFF) << "In a < expression, the "
        "computed value is out of range: " << value << Error();
      return 0xFF & value;
    } else {
      return std::nullopt;
    }
  }

  case ExpType::PLUS: {
    if (auto ao = Evaluate(assembly, exp->a.get(), Error)) {
      if (auto bo = Evaluate(assembly, exp->b.get(), Error)) {
        int64_t a = ao.value();
        int64_t b = bo.value();
        return {a + b};
      }
    }
    return std::nullopt;
  }
  case ExpType::MINUS: {
    if (auto ao = Evaluate(assembly, exp->a.get(), Error)) {
      if (auto bo = Evaluate(assembly, exp->b.get(), Error)) {
        int64_t a = ao.value();
        int64_t b = bo.value();
        return {a - b};
      }
    }
    return std::nullopt;
  }

  default:
    LOG(FATAL) << "Unknown/invalid expression in Evaluate.\n";
    return std::nullopt;
  }
}

static std::optional<uint16_t> Evaluate16(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= 0 && value <= 0xFFFF) << "Expected a 16-bit value "
      "but the expression's value was out of range: " << value << Error();
    return {(uint16_t)value};
  } else {
    return std::nullopt;
  }
}

static std::optional<uint8_t> Evaluate8(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= 0 && value <= 0xFF) << "Expected an 8-bit value "
      "but the expression's value was out of range: " << value << Error();
    return {(uint16_t)value};
  } else {
    return std::nullopt;
  }
}

static std::optional<int8_t> EvaluateSigned8(
    Assembly *assembly, const Exp *exp,
    const std::function<std::string()> &Error) {
  if (auto io = Evaluate(assembly, exp, Error)) {
    int64_t value = io.value();
    CHECK(value >= -128 && value <= 127) << "Expected a signed 8-bit value "
      "but the expression's value was out of range. This likely means that "
      "a relative jump was too far." << value << Error();
    return {(int8_t)value};
  } else {
    return std::nullopt;
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

// This is all possible addressing modes; not all are
// available for each mnemonic!
struct Addressing {
  enum Type {
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

  Addressing(Type t) : type(t) {}
  Addressing(Type t, std::shared_ptr<Exp> exp_in) :
    type(t), exp(std::move(exp_in)) {}
};


static std::shared_ptr<Exp> DisplacementExp(
    uint16_t base_address,
    std::shared_ptr<Exp> target_address) {
  return std::make_shared<Exp>(Exp{
      .type = ExpType::MINUS,
      .a = std::move(target_address),
      .b = std::make_shared<Exp>(Exp{
          .type = ExpType::NUMBER,
          .number = base_address,
        })
    });
}

static void Assemble(const std::string &asm_file,
                     const std::string &rom_file) {
  Assembly assembly;

  // Single-byte opcodes which have implied addressing.
  const std::unordered_map<std::string, uint8_t> mode_implied = {
    {"brk", 0x00},
    {"clc", 0x18},
    {"cld", 0xD8},
    {"clv", 0xB8},
    {"cli", 0x58},
    {"dex", 0xCA},
    {"dey", 0x88},
    {"inx", 0xE8},
    {"iny", 0xC8},
    {"nop", 0xEA},
    {"pha", 0x48},
    {"php", 0x08},
    {"pla", 0x68},
    {"plp", 0x28},
    {"rti", 0x40},
    {"rts", 0x60},
    {"sec", 0x38},
    {"sed", 0xF8},
    {"sei", 0x78},
    {"tax", 0xAA},
    {"tay", 0xA8},
    {"tsx", 0xBA},
    {"txa", 0x8A},
    {"txs", 0x9A},
    {"tya", 0x98},
  };

  // A typical instruction has the form
  // aaabbbcc, where aaa and cc determine the mnemonic.

  // Group 1: c=01
  // The map's values are aaa00001.
  const std::unordered_map<std::string, uint8_t> group1 = {
    {"ora", 0b00000001},
    {"and", 0b00100001},
    {"eor", 0b01000001},
    {"adc", 0b01100001},
    {"sta", 0b10000001},
    {"lda", 0b10100001},
    {"cmp", 0b11000001},
    {"sbc", 0b11100001},
  };

  // Group 2: c=10
  // The map's values are aaa00010.
  const std::unordered_map<std::string, uint8_t> group2 = {
    {"asl", 0b00000010},
    {"rol", 0b00100010},
    {"lsr", 0b01000010},
    {"ror", 0b01100010},
    {"stx", 0b10000010},
    {"ldx", 0b10100010},
    {"dec", 0b11000010},
    {"inc", 0b11100010},
  };

  // Group 3: c=00
  // JMP is not included here since it only has two addressing
  // modes, one of which is a special case.
  // The map's values are aaa00000.
  const std::unordered_map<std::string, uint8_t> group3 = {
    {"bit", 0b00100000},
    {"sty", 0b10000000},
    {"ldy", 0b10100000},
    {"cpy", 0b11000000},
    {"cpx", 0b11100000},
  };

  // Branches are all of the form xxy10000. xx describes a flag
  // and 1 is the comparison value. They take a signed displacement,
  // but the value in the map is the full opcode.
  const std::unordered_map<std::string, uint8_t> branches = {
    {"bpl", 0x10},
    {"bmi", 0x30},
    {"bvc", 0x50},
    {"bvs", 0x70},
    {"bcc", 0x90},
    {"bcs", 0xB0},
    {"bne", 0xD0},
    {"beq", 0xF0},
  };

  std::vector<std::string> lines = Util::ReadFileToLines(asm_file);

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

    auto CurrentBank = [&assembly, &Error]() -> Bank & {
        CHECK(!assembly.banks.empty()) << "There are no banks yet! "
          "Use .origin to start one." << Error();
        return assembly.banks.back();
      };

    auto EmitByte = [&CurrentBank](uint8_t v) {
        CurrentBank().bytes.push_back(v);
      };

    // Write the value of the expression as a little-endian 16-bit
    // word, either now or later.
    auto WriteExp16 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = Evaluate16(&assembly, exp.get(), Error)) {
            uint16_t v = bo.value();
            // printf(ACYAN("%04x") "\n", v);
            // Write in little-endian order.
            EmitByte(v & 0xFF);
            v >>= 8;
            EmitByte(v & 0xFF);

          } else {
            // printf(APURPLE("delayed") "\n");
            CurrentBank().delayed_16.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x00);
            EmitByte(0x00);
          }
        };

    // Same, for an 8-bit expression.
    auto WriteExp8 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = Evaluate8(&assembly, exp.get(), Error)) {
            uint8_t v = bo.value();
            EmitByte(v);
          } else {
            CurrentBank().delayed_8.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x00);
          }
        };

    auto WriteExpSigned8 =
      [&assembly, &EmitByte, &CurrentBank, &Error, line_num](
          std::shared_ptr<Exp> exp) {
          if (auto bo = EvaluateSigned8(&assembly, exp.get(), Error)) {
            int8_t v = bo.value();
            EmitByte(v);
            // EmitByte(0x99);
          } else {
            CurrentBank().delayed_s8.push_back(Delayed{
                .line_num = line_num,
                .exp = std::move(exp),
                .dest_addr = CurrentBank().NextAddress(),
              });

            EmitByte(0x77);
          }
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

    if (tokens.size() >= 2 &&
        tokens[0].type == SYMBOL &&
        tokens[1].type == COLON) {
      std::string symbol = std::move(tokens[0].str);
      CHECK(!assembly.HasSymbol(symbol)) << "Duplicate label: "
                                         << symbol << Error();
      uint16_t addr = CurrentBank().NextAddress();
      assembly.symbols[symbol] = addr;
      CurrentBank().debug_labels[addr] = symbol;

      // Remove the label.
      tokens.erase(tokens.begin(), tokens.begin() + 2);
    }

    // Nothing to do on empty lines (or lines with just a comment or label).
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
      };

    // Get the addressing mode for an instruction, which always begins
    // at the second token.
    auto GetAddressingMode =
      [&AddressingExp, &tokens, &Error]() -> Addressing {

        auto Program = AddressingExp << End<Token>();

        auto parseopt = Program(TokenSpan<Token>(tokens.data() + 1,
                                                 tokens.size() - 1));
        CHECK(parseopt.HasValue()) << "Expected addressing mode."
                                   << Error();
        return parseopt.Value();
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
        assembly.banks.emplace_back((int)tokens[2].num);

      } else if (dir == "db") {
        // Now read a series of expressions denoting bytes, and
        // write them.

        std::vector<std::shared_ptr<Exp>> exps =
          GetCommaSeparatedExpressions(2);

        for (const auto &e : exps) {
          WriteExp8(e);
        }

      } else if (dir == "dw") {
        // A series of expressions denoting words, and
        // write them.

        std::vector<std::shared_ptr<Exp>> exps =
          GetCommaSeparatedExpressions(2);

        for (const auto &e : exps) {
          WriteExp16(e);
        }

      } else {
        LOG(FATAL) << "Unknown directive: " << dir << Error();
      }

    } else if (tokens.size() > 2 &&
               tokens[0].type == SYMBOL &&
               tokens[1].type == EQUALS) {
      const std::string &sym = tokens[0].str;

      CHECK(!assembly.HasSymbol(sym)) << "Duplicate symbol "
                                      << sym << Error();

      // Symbolic constant.
      std::vector<std::shared_ptr<Exp>> exps =
        GetCommaSeparatedExpressions(2);
      CHECK(exps.size() == 1) << "Symbolic constant definition "
        "just takes one expression." << Error();

      auto io = Evaluate16(&assembly, exps[0].get(), Error);
      if (io.has_value()) {
        assembly.symbols[sym] = io.value();
      } else {
        assembly.delayed_symbols[sym] = Delayed{
          .line_num = line_num,
          .exp = std::move(exps[0]),
          .dest_addr = 0x0000,
        };
      }

    } else {
      CHECK(!tokens.empty() && tokens[0].type == SYMBOL) << "Expected "
        "instruction mnemonic." << Error();

      std::string mnemonic = Util::lcase(tokens[0].str);

      if (auto it = mode_implied.find(mnemonic); it != mode_implied.end()) {
        EmitByte(it->second);

      } else if (auto it = group1.find(mnemonic); it != group1.end()) {
        uint8_t opcode = it->second;

        Addressing mode = GetAddressingMode();

        // There's only one special case here (for STA, #), which doesn't
        // exist (and would not make sense).
        [&]{
          CHECK(mode.type != Addressing::ACCUMULATOR) << "Illegal addressing mode."
                                                      << Error();
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic != "sta") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'010'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X) {
            // For these two addressing modes, we could be using the zero page
            // version or the full 16-bit version. We don't know which one we
            // have without looking at the argument expression.
            //
            // Note this does not include ADDR_Y. There is no "zero page, Y"
            // mode in this group.
            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 : 0b000'101'00;
                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          switch (mode.type) {
          case Addressing::ACCUMULATOR:
          case Addressing::INDIRECT:
            LOG(FATAL) << "Illegal addressing mode."; break;
          case Addressing::INDIRECT_X:
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          case Addressing::INDIRECT_Y:
            EmitByte(opcode | 0b000'100'00);
            WriteExp8(mode.exp);
            return;
          default:
            break;
          }

          // Otherwise, we have a 16 bit version, either because we didn't
          // find an 8-bit value above or the expression's value is not
          // known yet.
          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          case Addressing::ADDR_Y: bbb = 0b000'110'00; break;
          default:
            LOG(FATAL) << "Bug: All cases should be covered by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);
        }();

      } else if (auto it = group2.find(mnemonic); it != group2.end()) {
        uint8_t opcode = it->second;

        Addressing mode = GetAddressingMode();

        [&]() {
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic == "ldx") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ACCUMULATOR) {
            CHECK(mnemonic == "asl" ||
                  mnemonic == "rol" ||
                  mnemonic == "lsr" ||
                  mnemonic == "ror") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'010'00);
            return;
          }

          // Whether ,X or ,Y versions are allowed depends on the
          // mnemonic. Check this for errors.
          if (mode.type == Addressing::ADDR_Y) {
            CHECK(mnemonic == "stx" || mnemonic == "ldx") << Error();
          }

          if (mnemonic == "stx" || mnemonic == "ldx") {
            CHECK(mode.type != Addressing::ADDR_X) << Error();
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X ||
              mode.type == Addressing::ADDR_Y) {
            // See if we are using the zero page version, as above.

            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 :
                  // ADDR_X and ADDR_Y are encoded the same.
                  0b000'101'00;
                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          if (mnemonic == "stx") {
            CHECK(mode.type != Addressing::ADDR_X) << "For mysterious "
              "reasons, 6502 does not support STX abs,x." << Error();
          }

          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          // Again, ADDR_X and ADDR_Y are encoded the same.
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          case Addressing::ADDR_Y: bbb = 0b000'111'00; break;
          default:
            LOG(FATAL) << "Bug: Should have been handled by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);

        }();


      } else if (auto it = group3.find(mnemonic); it != group3.end()) {
        uint8_t opcode = it->second;

        Addressing mode = GetAddressingMode();

        [&]() {
          if (mode.type == Addressing::IMMEDIATE) {
            CHECK(mnemonic == "ldy" ||
                  mnemonic == "cpy" ||
                  mnemonic == "cpx") << "Illegal addressing mode." << Error();
            EmitByte(opcode | 0b000'000'00);
            WriteExp8(mode.exp);
            return;
          }

          if (mode.type == Addressing::ADDR ||
              mode.type == Addressing::ADDR_X) {

            if (auto bo = Evaluate16(&assembly, mode.exp.get(), Error)) {
              uint16_t v = bo.value();

              if (v < 0x100) {
                // Zero page version.
                uint8_t bbb =
                  mode.type == Addressing::ADDR ? 0b000'001'00 : 0b000'101'00;

                if (mode.type == Addressing::ADDR_X) {
                  CHECK(mnemonic == "ldy" || mnemonic == "sty")
                    << "Illegal addressing mode." << Error();
                }

                EmitByte(opcode | bbb);
                // And the byte.
                EmitByte(v);
                return;
              }
            }
          }

          // We know this is the abs,x mode now (not zp,x), and LDY is
          // the only group 3 instruction that can use abs,x.
          if (mode.type == Addressing::ADDR_X) {
            CHECK(mnemonic == "ldy") << "Illegal addressing mode." << Error();
          }

          uint8_t bbb = 0;
          switch (mode.type) {
          case Addressing::ADDR: bbb = 0b000'011'00; break;
          case Addressing::ADDR_X: bbb = 0b000'111'00; break;
          default:
            LOG(FATAL) << "Bug: Should have been handled by now." << Error();
          }

          EmitByte(opcode | bbb);
          WriteExp16(mode.exp);

        }();

      } else if (auto it = branches.find(mnemonic); it != branches.end()) {

        uint8_t opcode = it->second;

        Addressing mode = GetAddressingMode();
        CHECK(mode.type == Addressing::ADDR) << "Branches are only allowed "
          "to absolute addresses." << Error();

        // The displacement is relative to the instruction that follows
        // this one (because the PC is incremented regardless). This is
        // two bytes after the current address.
        const uint16_t base_addr = CurrentBank().NextAddress() + 2;
        EmitByte(opcode);
        WriteExpSigned8(DisplacementExp(base_addr, mode.exp));

      } else if (mnemonic == "jmp") {

        // This is sort of a group 3 instruction, but handled separately.
        Addressing mode = GetAddressingMode();
        switch (mode.type) {
        case Addressing::ADDR:
          EmitByte(0x4C);
          WriteExp16(mode.exp);
          break;
        case Addressing::INDIRECT:
          EmitByte(0x6C);
          WriteExp16(mode.exp);
          break;
        default:
          LOG(FATAL) << "Invalid addressing mode for JMP." << Error();
          break;
        }

      } else if (mnemonic == "jsr") {
        EmitByte(0x20);

        std::vector<std::shared_ptr<Exp>> exps =
          GetCommaSeparatedExpressions(1);
        CHECK(exps.size() == 1) << "Expected a single 16-bit address "
          "for jsr." << Error();

        WriteExp16(exps[0]);

      } else {
        LOG(FATAL) << "Unimplemented instruction: " << tokens[0].str;
      }

    }

  }

  printf(AYELLOW("assembly") "\n");
  printf("  %d symbols\n", (int)assembly.symbols.size());
  printf("  %d delayed symbols\n", (int)assembly.delayed_symbols.size());

  printf("  There are %d banks.\n", (int)assembly.banks.size());
  for (const Bank &bank : assembly.banks) {
    printf("  " AWHITE("bank") "\n");
    printf("    %d delayed8\n", (int)bank.delayed_8.size());
    printf("    %d delayed16\n", (int)bank.delayed_16.size());
    printf("    %d bytes\n", (int)bank.bytes.size());
  }

  auto ErrorAt = [&lines](int line_num) {
      return std::function<std::string()>(
          [&lines, line_num]() {
            CHECK(line_num >= 0 && line_num < lines.size());
            return StringPrintf("\n On line %d:\n"
                                "%s\n",
                                line_num, lines[line_num].c_str());
          });
    };

  // Second pass: Resolve symbolic constants.
  // The best way to do this would be to generate a topological ordering
  // and detect any cycles. But if there exists such a thing, then
  // iteratively resolving them will terminate. In practice the number
  // of required passes is very small.
  {
    ArcFour rc("second pass");
    std::vector<std::pair<std::string, Delayed>> todo;
    todo.reserve(assembly.delayed_symbols.size());
    for (auto &[sym, delayed] : assembly.delayed_symbols) {
      todo.emplace_back(sym, std::move(delayed));
    }
    assembly.delayed_symbols.clear();

    while (!todo.empty()) {
      const size_t start_size = todo.size();

      std::vector<std::pair<std::string, Delayed>> remaining;

      // ... do a pass ...
      for (const auto &[sym, delayed] : todo) {
        auto Error = ErrorAt(delayed.line_num);
        auto io = Evaluate(&assembly, delayed.exp.get(), Error);
        if (io.has_value()) {
          printf("  " AGREY("%s") " => " ABLUE("%lld") "\n",
                 sym.c_str(), io.value());
          assembly.symbols[sym] = io.value();
        } else {
          remaining.emplace_back(sym, delayed);
        }
      }

      const size_t end_size = remaining.size();
      if (start_size == end_size) {
        fprintf(stderr,
                "In the second pass: Could not resolve symbolic "
                "constants. There is probably an undefined symbol "
                "or a cycle. These are the remaining symbols:\n");
        for (const auto &[sym, delayed] : todo) {
          fprintf(stderr, "  %s = %s\n",
                  sym.c_str(), ExpString(delayed.exp.get()).c_str());
        }
        LOG(FATAL) << "Failed due to unresolved symbols.";
      }

      todo = std::move(remaining);

      // To prevent pathological behavior (quadratic) when the
      // symbols are just in a reversed dependency order, attend
      // to them in a random order.
      Shuffle(&rc, &todo);
    }
  }

  // Third pass: Resolve delayed writes.
  for (Bank &bank : assembly.banks) {
    for (const Delayed &delayed : bank.delayed_16) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = Evaluate16(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed 16-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      uint16_t v = vo.value();
      bank.Write(delayed.dest_addr, v & 0xFF);
      v >>= 8;
      bank.Write(delayed.dest_addr + 1, v & 0xFF);
    }

    for (const Delayed &delayed : bank.delayed_8) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = Evaluate8(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed 8-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      uint8_t v = vo.value();
      bank.Write(delayed.dest_addr, v);
    }

    for (const Delayed &delayed : bank.delayed_s8) {
      auto Error = ErrorAt(delayed.line_num);
      auto vo = EvaluateSigned8(&assembly, delayed.exp.get(), Error);
      CHECK(vo.has_value()) << "Delayed signed 8-bit expression was "
        "not evaluatable in the second pass. It's probably "
        "using a label that was never defined: " <<
        ExpString(delayed.exp.get()) << Error();

      const int8_t v = vo.value();
      bank.Write(delayed.dest_addr, (uint8_t)v);
    }
  }

  // Now write the ROM, debugging symbols, and so on.
  CHECK(assembly.banks.size() == 1) << "Actually I only know how to "
    "write one bank right now, but this would be easily rectified.";

  // The ROM.
  Util::WriteFileBytes(rom_file, assembly.banks[0].bytes);
  printf("Wrote " AGREEN("%s") "\n", rom_file.c_str());

  // Debugging info.
  {
    std::string contents;
    for (const auto &[addr, name] : assembly.banks[0].debug_labels) {
      StringAppendF(&contents, "$%04x#%s#\n", addr, name.c_str());
    }
    std::string fbase = (std::string)Util::FileBaseOf(rom_file);
    std::string nlfile = StringPrintf("%s.nes.0.nl", fbase.c_str());
    Util::WriteFile(nlfile, contents);
    printf("Wrote " AGREEN("%s") "\n", nlfile.c_str());
  }
}

int main(int argc, char **argv) {
  ANSI::Init();


  CHECK(argc == 3) << "./asm7.exe source.nes out.rom\n";

  Assemble(argv[1], argv[2]);

  return 0;
}
