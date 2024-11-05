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
#include <string>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "re2/re2.h"

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

// A delayed 16-bit address. These are written after we've finished
// the first pass and know the value of every label. We simply
// write the 16-bit address to the 16-bit dest_addr.
//
// For clarity: The address here is a machine address, not an offset
// into assembled bytes (but the offset can be computed using the
// origin).
struct Delayed16 {
  std::string label;
  uint16_t dest_addr = 0;
};

// A delayed signed 8-bit displacement. These are measured from a
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




static void Assemble(const std::string &filename) {
  std::vector<std::string> lines = Util::ReadFileToLines(filename);

  std::vector<Bank> banks;

  for (int line_num = 0; line_num < (int)lines.size(); line_num++) {
    const std::string &line = lines[line_num];
    std::vector<Token> tokens = Tokenize(line_num, line);

    auto Error = [line_num, &line, &tokens]() {
        std::string toks;
        for (int t = 0; t < (int)tokens.size(); t++) {
          if (t > 0) toks.push_back(' ');
          StringAppendF(&toks, "%s",
                        TokenTypeString(tokens[t].type).c_str());
        }
        return StringPrintf("\nLine %d:\n%s\n%s", line_num,
                            line.c_str(), toks.c_str());
      };

    // Nothing to do on such lines.
    if (tokens.empty() || tokens[0].type == COMMENT)
      continue;

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

        LOG(FATAL) << "Unimplemented .db!" << Error();

      } else if (dir == "dw") {
        // A series of expressions denoting words, and
        // write them.

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
