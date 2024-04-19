
#include "frontend.h"

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "il.h"
#include "lexing.h"
#include "parsing.h"
#include "el.h"
#include "ansi.h"
#include "elaboration.h"
#include "timer.h"
#include "simplification.h"
#include "nullary.h"
#include "il-util.h"
#include "inclusion.h"
#include "uncurry.h"

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
  Timer load_timer;
  if (verbose > 0) {
    printf(AWHITE("Loading/lexing...") "\n");
    fflush(stdout);
  }

  const auto &[source, tokens, source_map] =
    Inclusion::Process(includepaths, filename);
  if (verbose > 1) {
    printf("%s\nBytes: %d\n", source.c_str(), (int)source.size());
  }

  const double load_sec = load_timer.Seconds();
  if (verbose > 0) {
    printf(AWHITE("Loaded/lexed %s") " in %s.\n",
           filename.c_str(), ANSI::Time(load_sec).c_str());
  }
  if (verbose > 1) {
    const auto &[csource, ctokens] =
      Lexing::ColorTokens(source, tokens);
    printf("Tokenization:\n"
           "%s\n"
           "%s\n",
           csource.c_str(),
           ctokens.c_str());
  }

  return RunFrontendInternal(source, tokens, source_map, options);
}

Program Frontend::RunFrontendOn(const std::string &error_context,
                                const std::string &contents,
                                Options options) {
  // Directly lex.
  if (verbose > 0) {
    printf(AWHITE("Lexing...") "\n");
    fflush(stdout);
  }

  Timer lex_timer;
  std::vector<Token> tokens = [&]{
    std::string lex_error;
    std::optional<std::vector<Token>> otokens =
      Lexing::Lex(contents, &lex_error);
    CHECK(otokens.has_value()) << "Lexing " << error_context << ":\n"
            "Invalid syntax: " << lex_error;
    return std::move(otokens.value());
  }();

  SourceMap source_map = Inclusion::SimpleSourceMap(error_context, contents);

  const double lex_sec = lex_timer.Seconds();
  if (verbose > 1) {
    const auto &[source, ctokens] =
      Lexing::ColorTokens(contents, tokens);
    printf(AWHITE("Lexed") " in %s:\n"
           "%s\n%s\n",
           ANSI::Time(lex_sec).c_str(),
           source.c_str(), ctokens.c_str());
  }

  return RunFrontendInternal(contents, tokens, source_map, options);
}

Program Frontend::RunFrontendInternal(
    const std::string &contents,
    const std::vector<Token> &tokens,
    const SourceMap &source_map,
    Options options) {

  if (verbose > 0) {
    printf("\n" AWHITE("Parsing...") "\n");
    fflush(stdout);
  }

  Timer parse_timer;
  const el::Exp *el_exp =
    Parsing::Parse(&el_pool, source_map, contents, tokens);
  // Parser reports its own errors.
  CHECK(el_exp != nullptr);
  const double parse_sec = parse_timer.Seconds();

  if (verbose > 0) {
    printf(AWHITE("Parsed") " in %s.\n",
           ANSI::Time(parse_sec).c_str());
  }

  if (verbose > 1) {
    printf("%s\n",
           el::ExpString(el_exp).c_str());
    fflush(stdout);
  }

  Timer rewrite_timer;
  el::Nullary nullary(&el_pool);
  el_exp = nullary.Rewrite(el_exp);
  el::Uncurry uncurry(&el_pool);
  el_exp = uncurry.Rewrite(el_exp);
  const double rewrite_sec = rewrite_timer.Seconds();
  if (verbose > 1) {
    printf(AWHITE("EL rewrites") " in %s:\n"
           "%s\n",
           ANSI::Time(rewrite_sec).c_str(),
           el::ExpString(el_exp).c_str());
    fflush(stdout);
  }


  if (verbose > 0) {
    printf("\n" AWHITE("Elaborating...") "\n");
  }

  Timer elab_timer;
  Elaboration elaboration(source_map, &el_pool, &il_pool);
  Program il_pgm = elaboration.Elaborate(el_exp);
  const double elab_sec = elab_timer.Seconds();

  if (verbose > 0) {
    printf(AWHITE("Elaborated") " in %s.\n",
           ANSI::Time(elab_sec).c_str());
    if (verbose > 1) {
      printf("%s\n",
             il::ProgramString(il_pgm).c_str());
      fflush(stdout);
    }
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
    if (verbose > 0) {
      printf("\n" AWHITE("Simplifying...") "\n");
    }
    Timer simplify_timer;
    Simplification simplification(&il_pool);
    il_pgm = simplification.Simplify(il_pgm);
    double simplify_sec = simplify_timer.Seconds();

    if (verbose > 0) {
      // TODO: Get stats.
      printf(AWHITE("Simplified") " in %s:\n",
             ANSI::Time(simplify_sec).c_str());

      if (verbose > 1) {
        printf(AFGCOLOR(200, 230, 200, "%s") "\n",
               il::ProgramString(il_pgm).c_str());
        fflush(stdout);
      }
    }
  }

  // Timing summary?

  return il_pgm;
}
