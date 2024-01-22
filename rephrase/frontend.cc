
#include "frontend.h"

#include <string>

#include "base/logging.h"
#include "lex.h"
#include "parse.h"
#include "ast.h"
#include "util.h"
#include "ansi.h"
#include "elaboration.h"

using Token = el::Token;
using Lexing = el::Lexing;
using Parsing = el::Parsing;

void Frontend::SetVerbose(int v) {
  verbose = v;
}

void Frontend::AddIncludePath(const std::string &s) {
  includepaths.push_back(s);
}

const il::Exp *Frontend::RunFrontend(const std::string &filename) {
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
    printf("\n" AWHITE("Parsing...") "\n");
    fflush(stdout);
  }

  const el::Exp *el_pgm = Parsing::Parse(&el_pool, contents, tokens);
  // Parser reports its own errors.
  CHECK(el_pgm != nullptr);

  // Maybe only at higher verbosity...
  if (verbose > 0) {
    printf(AWHITE("Parsed") ":\n"
           "%s\n", el::ExpString(el_pgm).c_str());
    fflush(stdout);
    printf("\n" AWHITE("Elaborating...") "\n");
  }

  // Pass initial context?
  Elaboration elaboration(&il_pool);
  const il::Exp *il_pgm = elaboration.Elaborate(el_pgm);

  return il_pgm;
}
