
#include "frontend.h"

#include <string>

#include "base/logging.h"
#include "lex.h"
#include "parse.h"
#include "ast.h"
#include "util.h"
#include "ansi.h"

void Frontend::SetVerbose(int v) {
  verbose = v;
}

void Frontend::AddIncludePath(const std::string &s) {
  includepaths.push_back(s);
}

const Exp *Frontend::RunFrontend(const std::string &filename) {
  const std::string contents = Util::ReadFile(filename);

  if (verbose > 0) {
    printf(AWHITE("Lexing...") "\n");
    fflush(stdout);
  }

  const std::vector<Token> tokens = [&]{
    std::string lex_error;
    std::optional<std::vector<Token>> otokens =
      Lexing::Lex(contents, &lex_error);
    CHECK(otokens.has_value()) << "Lexing " << filename << ":\n"
            "Invalid syntax: " << lex_error;
    return std::move(otokens.value());
  }();

  if (verbose > 0) {
    const auto &[source, ctokens] =
      Lexing::ColorTokens(contents, tokens);
    printf(AWHITE("Lexed") ":\n"
           "%s\n%s\n", source.c_str(), ctokens.c_str());
    fflush(stdout);
  }

  const Exp *pgm = Parsing::Parse(&pool, contents, tokens);
  // Parser reports its own errors.
  CHECK(pgm != nullptr);

  // TODO: Elaboration.

  return pgm;
}
