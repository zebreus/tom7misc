
#include "frontend.h"

#include <string>

#include "base/logging.h"
#include "lex.h"
#include "parse.h"
#include "el.h"
#include "util.h"
#include "ansi.h"
#include "elaboration.h"
#include "timer.h"

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
  Timer read_timer;
  const std::string contents = Util::ReadFile(filename);
  const double read_sec = read_timer.Seconds();
  if (verbose > 0) {
    printf(AWHITE("Loaded %s") " in %s.\n",
           filename.c_str(),
           ANSI::Time(read_sec).c_str());
  }

  return RunFrontendOn(filename, contents);
}

const il::Exp *Frontend::RunFrontendOn(const std::string &filename,
                                       const std::string &contents) {
  if (verbose > 0) {
    printf(AWHITE("Lexing...") "\n");
    fflush(stdout);
  }

  Timer tokenize_timer;
  const std::vector<Token> tokens = [&]{
    std::string lex_error;
    std::optional<std::vector<Token>> otokens =
      Lexing::Lex(contents, &lex_error);
    CHECK(otokens.has_value()) << "Lexing " << filename << ":\n"
            "Invalid syntax: " << lex_error;
    return std::move(otokens.value());
  }();
  const double tokenize_sec = tokenize_timer.Seconds();

  if (verbose > 0) {
    const auto &[source, ctokens] =
      Lexing::ColorTokens(contents, tokens);
    printf(AWHITE("Lexed") " in %s:\n"
           "%s\n%s\n",
           ANSI::Time(tokenize_sec).c_str(),
           source.c_str(), ctokens.c_str());
    printf("\n" AWHITE("Parsing...") "\n");
    fflush(stdout);
  }

  Timer parse_timer;
  const el::Exp *el_pgm = Parsing::Parse(&el_pool, contents, tokens);
  // Parser reports its own errors.
  CHECK(el_pgm != nullptr);
  const double parse_sec = parse_timer.Seconds();

  // Maybe only at higher verbosity...
  if (verbose > 0) {
    printf(AWHITE("Parsed") " in %s:\n"
           "%s\n",
           ANSI::Time(parse_sec).c_str(),
           el::ExpString(el_pgm).c_str());
    fflush(stdout);
    printf("\n" AWHITE("Elaborating...") "\n");
  }

  Timer elab_timer;
  Elaboration elaboration(&il_pool);
  const il::Exp *il_pgm = elaboration.Elaborate(el_pgm);
  const double elab_sec = elab_timer.Seconds();

  if (verbose > 0) {
    printf(AWHITE("Elaborated") " in %s:\n"
           "%s\n",
           ANSI::Time(elab_sec).c_str(),
           il::ExpString(il_pgm).c_str());
    fflush(stdout);
    printf("\n" AWHITE("Elaborating...") "\n");
  }

  return il_pgm;
}
