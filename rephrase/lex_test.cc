
#include "lex.h"

#include <string>
#include <vector>
#include <deque>
#include <cstdint>

#include "ast.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

static void PrintTokens(const std::string &input,
                        const std::vector<Token> &tokens) {
  const auto &[source, ctokens] = ColorTokens(input, tokens);
  printf("Got:\n"
         "%s\n%s\n",
         source.c_str(), ctokens.c_str());
}

#define CHECK_LEX(str, ...) do { \
    const std::string input = str; \
    const std::vector<Token> tokens = Lex(input); \
    const std::vector<TokenType> expected = {__VA_ARGS__};  \
    bool ok = tokens.size() == expected.size();   \
    for (int i = 0; i < (int)tokens.size() &&     \
           i < (int)expected.size(); i++) {           \
      if (tokens[i].type != expected[i]) ok = false;  \
    }                                               \
    if (!ok) {                                      \
      PrintTokens(input, tokens);                       \
      /* XXX print expected tokens too */                   \
      CHECK(false) << "Did not get expected token types.";  \
    }                                                       \
    } while (0)

static void TestLex() {

  {
    const std::string input =
      "the   cat fn() went to 1234 the \"string\" store\n"
      "where he \"\\\\slashed\\n\" t-i-r-e-s=>\n"
      "Here -> is a [nested [123] expression].\n";
    const std::vector<Token> tokens =
      Lex(input);
    const auto &[source, ctokens] = ColorTokens(input, tokens);
    printf("%s\n%s\n", source.c_str(), ctokens.c_str());
  }

  CHECK_LEX("15232", DIGITS);
  CHECK_LEX("0x15232", NUMERIC_LIT);
  CHECK_LEX("0x0000.0000.0000.0000", NUMERIC_LIT);
  CHECK_LEX("0b1010100", NUMERIC_LIT);
  CHECK_LEX("0u2A03", NUMERIC_LIT);
  CHECK_LEX("0o1234", NUMERIC_LIT);

  CHECK_LEX("1e100", FLOAT_LIT);
  CHECK_LEX("1e-100", FLOAT_LIT);
  CHECK_LEX(".1e-100", FLOAT_LIT);
  CHECK_LEX("1.e+100", FLOAT_LIT);

  CHECK_LEX("-> |", ARROW, BAR);
  CHECK_LEX("= =>", EQUALS, DARROW);

  CHECK_LEX("->|", ID);
  CHECK_LEX("==>", ID);
  CHECK_LEX("add-to-alist", ID);
}


int main(int argc, char **argv) {
  ANSI::Init();
  TestLex();

  printf("OK");
  return 0;
}
