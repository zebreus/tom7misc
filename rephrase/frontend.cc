
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
#include "simplification.h"
#include "nullary.h"
#include "il-util.h"

using Token = el::Token;
using Lexing = el::Lexing;
using Parsing = el::Parsing;
using Simplification = il::Simplification;
using Program = il::Program;
using ILUtil = il::ILUtil;

void Frontend::SetVerbose(int v) {
  verbose = v;
}

void Frontend::AddIncludePath(const std::string &s) {
  includepaths.push_back(s);
}

Program Frontend::RunFrontend(const std::string &filename,
                              Options options) {
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

Program Frontend::RunFrontendOn(const std::string &filename,
                                const std::string &contents,
                                Options options) {
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
  const el::Exp *el_exp = Parsing::Parse(&el_pool, contents, tokens);
  // Parser reports its own errors.
  CHECK(el_exp != nullptr);
  const double parse_sec = parse_timer.Seconds();

  // Maybe only at higher verbosity...
  if (verbose > 0) {
    printf(AWHITE("Parsed") " in %s:\n"
           "%s\n",
           ANSI::Time(parse_sec).c_str(),
           el::ExpString(el_exp).c_str());
    fflush(stdout);
  }

  Timer nullary_timer;
  el::Nullary nullary(&el_pool);
  el_exp = nullary.Rewrite(el_exp);
  const double nullary_sec = nullary_timer.Seconds();
  if (verbose > 1) {
    printf(AWHITE("Nullary rewrite") " in %s:\n"
           "%s\n",
           ANSI::Time(nullary_sec).c_str(),
           el::ExpString(el_exp).c_str());
    fflush(stdout);
  }


  if (verbose > 0) {
    printf("\n" AWHITE("Elaborating...") "\n");
  }

  Timer elab_timer;
  Elaboration elaboration(&el_pool, &il_pool);
  Program il_pgm = elaboration.Elaborate(el_exp);
  const double elab_sec = elab_timer.Seconds();

  if (verbose > 0) {
    printf(AWHITE("Elaborated") " in %s:\n"
           "%s\n",
           ANSI::Time(elab_sec).c_str(),
           il::ProgramString(il_pgm).c_str());
    fflush(stdout);
  }

  Timer finalize_evars_timer;
  il_pgm = ILUtil::FinalizeEVars(&il_pool, il_pgm);
  const double finalize_evars_sec = finalize_evars_timer.Seconds();
  if (verbose > 1) {
    printf(AWHITE("Finalized evars") " in %s:\n"
           "%s\n",
           ANSI::Time(finalize_evars_sec).c_str(),
           il::ProgramString(il_pgm).c_str());
    fflush(stdout);
  }

  if (options.simplify) {
    printf("\n" AWHITE("Simplifying...") "\n");
    Timer simplify_timer;
    Simplification simplification(&il_pool);
    il_pgm = simplification.Simplify(il_pgm);
    double simplify_sec = simplify_timer.Seconds();

    if (verbose > 0) {
      printf(AWHITE("Simplified") " in %s:\n"
             AFGCOLOR(200, 230, 200, "%s") "\n",
             ANSI::Time(simplify_sec).c_str(),
             il::ProgramString(il_pgm).c_str());
      fflush(stdout);
    }
  }

  return il_pgm;
}
