
#include "lexing.h"

#include <optional>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

namespace el {

static void PrintTokens(const std::string &input,
                        const std::vector<Token> &tokens) {
  const auto &[source, ctokens] = Lexing::ColorTokens(input, tokens);
  Print("Got:\n"
        "{}\n{}\n",
        source, ctokens);
}

#define CHECK_LEX(str, ...)                                    \
  do {                                                         \
    const std::string input = str;                             \
    std::string error;                                         \
    const std::optional<std::vector<Token>> otokens =          \
        Lexing::Lex(input, &error);                            \
    CHECK(otokens.has_value()) << "Did not lex: " << error;    \
    const auto &tokens = otokens.value();                      \
    const std::vector<TokenType> expected = {__VA_ARGS__};     \
    bool ok = tokens.size() == expected.size();                \
    for (int i = 0;                                            \
         i < (int)tokens.size() && i < (int)expected.size();   \
         i++) {                                                \
      if (tokens[i].type != expected[i])                       \
        ok = false;                                            \
    }                                                          \
    if (!ok) {                                                 \
      PrintTokens(input, tokens);                              \
      /* XXX print expected tokens too */                      \
      CHECK(false) << "Did not get expected token types.";     \
    }                                                          \
  } while (0)

static void TestLex() {

  {
    const std::string input =
      "the   cat fn() went to 1234 the \"string\" store\n"
      "where he \"\\\\slashed\\n\" t-i-r-e-s=>\n"
      "Here -> is a [nested [123] expression].\n";
    const std::optional<std::vector<Token>> tokens =
      Lexing::Lex(input, nullptr);
    CHECK(tokens.has_value());
    const auto &[source, ctokens] =
      Lexing::ColorTokens(input, tokens.value());
    Print("{}\n{}\n", source, ctokens);
  }

  CHECK_LEX("15232", DIGITS);
  CHECK_LEX("0x15232", NUMERIC_LIT);
  CHECK_LEX("0x0000.0000.0000.0000", NUMERIC_LIT);
  CHECK_LEX("0b1010100", NUMERIC_LIT);
  CHECK_LEX("0u2A03", NUMERIC_LIT);
  CHECK_LEX("0o1234", NUMERIC_LIT);
  CHECK_LEX("-77", NUMERIC_LIT);

  CHECK_LEX("1e100", FLOAT_LIT);
  CHECK_LEX("1e-100", FLOAT_LIT);
  CHECK_LEX(".1e-100", FLOAT_LIT);
  CHECK_LEX("1.e+100", FLOAT_LIT);
  CHECK_LEX("8.8817841970012523e-16", FLOAT_LIT);
  CHECK_LEX("-999.0", FLOAT_LIT);

  CHECK_LEX("true", TRUE);
  CHECK_LEX(" false ", FALSE);

  CHECK_LEX("-> |", ARROW, BAR);
  CHECK_LEX("= =>", EQUALS, DARROW);

  CHECK_LEX("->|", ID);
  CHECK_LEX("==>", ID);
  CHECK_LEX("add-to-alist", ID);

  CHECK_LEX("::", ID);
  CHECK_LEX("a:b", ID, COLON, ID);
  CHECK_LEX("#1/2", HASH, DIGITS, SLASH, DIGITS);

  CHECK_LEX("(_ as x)", LPAREN, UNDERSCORE, AS, ID, RPAREN);
  CHECK_LEX("the (* comment *) 777", ID, DIGITS);
  CHECK_LEX("and (* a (* nested *)*) 1", AND, DIGITS);

  CHECK_LEX("[Here is some layout with [* a comment *].]",
            LBRACKET, LAYOUT_LIT,
            LBRACKET, LAYOUT_COMMENT, RBRACKET,
            LAYOUT_LIT, RBRACKET);


  CHECK_LEX("0'c'", NUMERIC_LIT);
  CHECK_LEX("0'∃'", NUMERIC_LIT);
  // TODO: Should allow 0'\'', right?

  CHECK_LEX("\"string\\nliteral\"", STR_LIT);
}

}  // namespace el

int main(int argc, char **argv) {
  ANSI::Init();
  el::TestLex();

  Print("OK\n");
  return 0;
}

