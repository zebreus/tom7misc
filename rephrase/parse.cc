
#include "parse.h"

#include <string>
#include <vector>
#include <cstdint>

#include "parser-combinators.h"
#include "ast.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "lex.h"

static std::string UnescapeStrLit(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (int i = 0; i < (int)s.size(); i++) {
    const char c = s[i];
    if (c == '\\') {
      CHECK(i < (int)s.size() - 1) << "Bug: Trailing escape "
        "character in string literal.";
      i++;
      const char d = s[i];
      switch (d) {
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case '\\': out.push_back('\\'); break;
      case '\"': out.push_back('\"'); break;
      default:
        // TODO: Implement \x and \u{1234} stuff.
        CHECK(false) << "Unimplemented or illegal escape "
                     << StringPrintf("\\%c", d)
                     << " in string literal.";
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Nothing to do: There are no escaped characters in layout.
static inline std::string UnescapeLayoutLit(const std::string &s) {
  return s;
}

template<TokenType t>
struct IsToken {
  using token_type = Token;
  using out_type = Token;
  constexpr IsToken() {}
  Parsed<Token> operator()(std::span<const Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None();
    if (toks[0].type == t) return Parsed(toks[0], 1);
    else return Parsed<Token>::None();
  }
};


const Exp *Parse(AstPool *pool, const std::string &input) {
  std::vector<Token> tokens = Lex(input);
  const auto &[source, ctokens] = ColorTokens(input, tokens);
  printf("%s\n%s\n", source.c_str(), ctokens.c_str());

  auto TokenStr = [&input](Token t) {
      CHECK(t.start <= input.size());
      CHECK(t.start + t.length <= input.size());
      return std::string(input.substr(t.start, t.length));
    };

  // TODO: Support other integer literals.
  const auto Int = IsToken<DIGITS>() >[&](Token t) {
      std::string s = TokenStr(t);
      int64_t i = std::stoll(s);
      CHECK(StringPrintf("%lld", i) == s) << "Invalid integer "
        "literal " << s;
      return i;
    };

  const auto Id = IsToken<ID>() >[&](Token t) { return TokenStr(t); };
  const auto StrLit = IsToken<STR_LIT>() >[&](Token t) {
      // Remove leading and trailing double quotes. Process escapes.
      std::string s = TokenStr(t);
      CHECK(s.size() >= 2) << "Bug: The double quotes are included "
        "in the token.";
      return UnescapeStrLit(s.substr(1, s.size() - 2));
    };

  const auto LayoutLit = IsToken<LAYOUT_LIT>() >[&](Token t) {
      return UnescapeLayoutLit(TokenStr(t));
    };

  // Patterns.

  const auto TuplePat = [&](const auto &Pattern) {
      return ((IsToken<LPAREN>() >>
               Separate0(Pattern, IsToken<COMMA>()) <<
               IsToken<RPAREN>())
              >[&](const std::vector<const Pat *> &ps) -> const Pat * {
                  if (ps.size() == 1) {
                    // Then this is just a parenthesized pattern.
                    return ps[0];
                  } else {
                    return pool->TuplePat(ps);
                  }
                });
    };


  const auto Pattern =
    Fix<Token, const Pat *>([&](const auto &Self) {
        return
          (Id >[&](const std::string &s) { return pool->VarPat(s); }) ||
          (IsToken<UNDERSCORE>() >[&](auto) { return pool->WildPat(); }) ||
          TuplePat(Self);
      });


  // Expressions.

  const auto IntExpr = Int >[&](int64_t i) { return pool->Int(i); };
  const auto VarExpr = Id >[&](const std::string &s) {
      return pool->Var(s);
    };
  const auto StrLitExpr = StrLit >[&](const std::string &s) {
      return pool->Str(s);
    };

  // Either (), or (e) or (e1, e2, ...).
  const auto TupleExpr = [&](const auto &Expr) {
      return ((IsToken<LPAREN>() >>
               Separate0(Expr, IsToken<COMMA>()) <<
               IsToken<RPAREN>())
              >[&](const std::vector<const Exp *> &es) {
                  if (es.size() == 1) {
                    // Then this is just a parenthesized expression.
                    return es[0];
                  } else {
                    return pool->Tuple(es);
                  }
                });
    };


  const auto LayoutExpr = [&](const auto &Expr) {
      const auto Lay =
        Fix<Token, const Layout *>([&](const auto &Self) {
            return (LayoutLit &&
              *((IsToken<LBRACKET>() >> Expr << IsToken<RBRACKET>()) &&
                LayoutLit))
              >[&](const auto &p) {
                  const auto &[l1, v] = p;
                  const Layout *x1 = pool->TextLayout(l1);
                  if (v.empty()) {
                    // No need for a join node.
                    return x1;
                  } else {
                    std::vector<const Layout *> joinme;
                    joinme.reserve(1 + 2 * v.size());
                    joinme.push_back(x1);
                    for (const auto &[e, t] : v) {
                      joinme.push_back(pool->ExpLayout(e));
                      joinme.push_back(pool->TextLayout(t));
                    }
                    return pool->JoinLayout(std::move(joinme));
                  }
                };
          });

      return (IsToken<LBRACKET>() >> Lay << IsToken<RBRACKET>())
          >[&](const Layout *lay) {
              return pool->LayoutExp(lay);
            };
    };

  const auto LetExpr = [&](const auto &Expr, const auto &Decl) {
      return ((IsToken<LET>() >> *Decl << IsToken<IN>()) &&
              // TODO: Can allow sequence here
              (Expr << IsToken<END>())) >[&](const auto &p) {
                  const auto &[ds, e] = p;
                  return pool->Let(ds, e);
                };
    };

  const auto IfExpr = [&](const auto &Expr) {
      return ((IsToken<IF>() >> Expr) &&
              (IsToken<THEN>() >> Expr) &&
              (IsToken<ELSE>() >> Expr))
        >[&](const auto &p) {
            const auto &[pp, f] = p;
            const auto &[cond, t] = pp;
            return pool->If(cond, t, f);
          };
    };


  // This is syntactic sugar for val _ = e
  const auto DoDecl = [&](const auto &Expr) {
      return (IsToken<DO>() >> Expr)
        >[&](const Exp *e) {
            return pool->ValDec(pool->WildPat(), e);
          };
    };

  const auto ValDecl = [&](const auto &Expr) {
      return ((IsToken<VAL>() >> Pattern) &&
              (IsToken<EQUALS>() >> Expr))
        >[&](const auto &p) {
            return pool->ValDec(p.first, p.second);
          };
    };

  const auto FunDecl = [&](const auto &Expr) {
      return ((IsToken<FUN>() >> (Id && Pattern)) &&
              (IsToken<EQUALS>() >> Expr))
        >[&](const auto &p) {
            const auto &[f, pat] = p.first;
            return pool->FunDec(f, pat, p.second);
          };
    };

  const auto AdjApp = [&](const Exp *f, const Exp *arg) -> const Exp * {
      return pool->App(f, arg);
    };

  // TODO: This currently just parses a sequence of function
  // applications in a convoluted way. Need to also pass
  // operators with fixity information in this list!
  const auto ResolveExprFixity = [&](const std::vector<const Exp *> &exps) -> std::optional<const Exp *> {
      // We'd get the same result from the code below, but with
      // more work in this very common case.
      if (exps.size() == 1) return exps[0];

      using Item = FixityItem<const Exp *>;
      std::vector<Item> items;
      items.reserve(exps.size());
      for (const Exp *exp : exps) {
        Item item;
        item.fixity = Fixity::Atom;
        item.item = exp;
        items.push_back(std::move(item));
      }

      std::optional<const Exp *> resolved =
        ResolveFixityAdj<const Exp *>(
            items, Associativity::Left,
            std::function<const Exp *(const Exp *, const Exp *)>(AdjApp),
            nullptr);

      return resolved;
    };

  // Expression and Declaration are mutually recursive.
  const auto &[Expr, Decl] =
    Fix2<Token, const Exp *, const Dec *>(
        [&](const auto &Expr, const auto &Decl) {

          // Expression parser.
          auto AtomicExpr =
            IntExpr ||
            VarExpr ||
            StrLitExpr ||
            // Includes parenthesized expression.
            TupleExpr(Expr) ||
            LayoutExpr(Expr) ||
            LetExpr(Expr, Decl) ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Exp *>();

          auto AppExpr =
            +AtomicExpr /= ResolveExprFixity;

          return IfExpr(Expr) || AppExpr;
        },
        [&](const auto &Expr, const auto &Decl) {
          // Declaration parser.
          return
            DoDecl(Expr) ||
            ValDecl(Expr) ||
            FunDecl(Expr) ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Dec *>();
        });

  auto Program = Expr << End<Token>();

  auto po = Program(std::span<Token>(tokens.data(), tokens.size()));
  CHECK(po.HasValue()) << "Could not parse program.";
  return po.Value();
}
