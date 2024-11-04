// Simple 6502 assembler.
// Goal is to reproduce mario.nes (or really mario.prg) from mario.asm,
// following what x816 would do (that assembler seems to be so old that
// it needs DOSBox or similar to run!). I also want to generate the
// memory maps (.nl files) for the fceux debugger.

#include "ansi.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
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

static void Assemble(const std::string &filename) {
  std::vector<std::string> lines = Util::ReadFileToLines(filename);

  for (int line_num = 0; line_num < (int)lines.size(); line_num++) {
    const std::string &line = lines[line_num];
    std::vector<Token> tokens = Tokenize(line_num, line);
  }

  printf("OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();


  CHECK(argc == 3) << "./asm7.exe source.nes out.rom\n";

  Assemble(argv[1]);

  return 0;
}
