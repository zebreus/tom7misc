
#include "parse.h"

#include <span>
#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "el.h"
#include "lex.h"
#include "parser-combinators.h"
#include "util.h"

namespace el {

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

// For built-in identifiers, get their fixity, associativity, and precedence.
// TODO: Make it possible to declare new fixity.
static std::tuple<Fixity, Associativity, int>
GetFixity(const std::string &sym) {
  static const std::unordered_map<std::string,
                                  std::tuple<Fixity, Associativity, int>>
    builtin = {
    {"o", {Fixity::Infix, Associativity::Left, 2}},
    // Maybe add explicit floating-point versions
    {"+", {Fixity::Infix, Associativity::Left, 4}},
    {"-", {Fixity::Infix, Associativity::Left, 4}},
    {"*", {Fixity::Infix, Associativity::Left, 6}},
    // int * int -> float
    {"/", {Fixity::Infix, Associativity::Left, 6}},
    {"div", {Fixity::Infix, Associativity::Left, 6}},
    {"mod", {Fixity::Infix, Associativity::Left, 6}},

  };
  auto it = builtin.find(sym);
  if (it != builtin.end()) return it->second;

  // Defaults based on what characters it uses.
  CHECK(!sym.empty()) << "Invalid empty symbol.";
  char c = sym[0];
  if ((c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z')) {
    // Alpha identifiers are atoms.
    return std::make_tuple(Fixity::Atom, Associativity::Non, 0);
  } else {
    // Symbolic identifiers are infix.
    return std::make_tuple(Fixity::Infix, Associativity::Left, 5);
  }
}

const Exp *Parsing::Parse(AstPool *pool,
                          const std::string &input,
                          const std::vector<Token> &tokens) {

  auto TokenStr = [&input](Token t) {
      CHECK(t.start <= input.size());
      CHECK(t.start + t.length <= input.size());
      return std::string(input.substr(t.start, t.length));
    };

  const auto Int = IsToken<DIGITS>() >[&](Token t) {
      std::string s = TokenStr(t);
      int64_t i = std::stoll(s);
      CHECK(StringPrintf("%lld", i) == s) << "Invalid integer "
        "literal " << s;
      return i;
    };

  // TODO: Support other integer literals. (NUMERIC_LIT)
  // TODO: Parse bigint
  const auto BigInteger = Int >[&](int64_t i) {
      return BigInt(i);
    };

  const auto Float = IsToken<FLOAT_LIT>() >[&](Token t) -> double {
      std::string s = TokenStr(t);
      std::optional<double> od = Util::ParseDoubleOpt(s);
      if (od.has_value()) {
        return od.value();
      } else {
        LOG(FATAL) << "Illegal float literal: " << s;
        return 0.0;
      }
    };

  // XXX Probably need to add some keywords like * and /
  const auto Id = IsToken<ID>() >[&](Token t) { return TokenStr(t); };
  // Labels can also be numeric.
  const auto Label = Id || (IsToken<DIGITS>() >[&](Token t) {
      return TokenStr(t);
    });

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

  // Types.


  // Parse infix operators, e.g. T -> T * T
  using TypeFixityElt = FixityItem<const Type *>;
  const auto ResolveTypeFixity = [&](const std::vector<TypeFixityElt> &elts) ->
    std::optional<const Type *> {
    // Type application is handled below, so we do not need an "adj" case.
    return ResolveFixity<const Type *>(elts, nullptr);
  };

  const TypeFixityElt ArrowElement = {
    .fixity = Fixity::Infix,
    // a -> b -> c  is a -> (b -> c)
    .assoc = Associativity::Right,
    .precedence = 9,
    .binop = [&](const Type *a, const Type *b) {
        return pool->Arrow(a, b);
      },
  };

  const auto TypeExpr =
    Fix<Token, const Type *>([&](const auto &Self) {
        auto RecordType =
          (IsToken<LBRACE>() >>
           Separate0(
               (Label && (IsToken<COLON>() >> Self)),
               IsToken<COMMA>()) << IsToken<RBRACE>())
          >[&](const std::vector<std::pair<std::string, const Type *>> &v) {
              // We don't do any normalization in EL.
              return pool->RecordType(v);
            };

        // This returns a vector of comma-separated types.
        // Non-degenerate comma-separated types are only used
        // in "(int, string) map" situations, but we also
        // parse degenerate parenthesized types here, which can
        // include "(int)" and "()".
        auto CommaType =
          (IsToken<LPAREN>() >>
           Separate0(Self, IsToken<COMMA>()) <<
           IsToken<RPAREN>());

        // An atomic type that doesn't start with (.
        auto UnparenthesizedAtomicType =
          RecordType ||
          Id >[&](const std::string &s) { return pool->VarType(s, {}); };

        auto AtomicType =
          // Use CommaType to parse parenthesized expressions.
          // We don't allow () for the empty tuple, but we could.
          (CommaType /=[&](const std::vector<const Type *> &v) ->
           std::optional<const Type *> {
            if (v.size() == 1) return v[0];
            else return std::nullopt;
          }) ||
          UnparenthesizedAtomicType;

        // Vector of type args for type application.
        auto AppArg =
          CommaType ||
          UnparenthesizedAtomicType >[&](const Type *t) {
              return std::vector<const Type *>{t};
            };

        // This can be "(int, string) t1 t2 t3"
        // or int t1 t2
        // or int
        // but not (int, string)
        auto AppType =
          (AppArg && *Id) /=[&](const auto &pair) ->
          std::optional<const Type *>{
              const auto &[varg, vapps] = pair;
              if (vapps.empty()) {
                // then this may just be a single type with
                // no applications
                if (varg.size() == 1) {
                  return {varg[0]};
                } else {
                  // Otherwise we have something like (int, string)
                  // on its own, which is not allowed.
                  return std::nullopt;
                }
              } else {
                // Postfix applications.
                const Type *t = pool->VarType(vapps[0], varg);
                for (int i = 1; i < (int)vapps.size(); i++) {
                  t = pool->VarType(vapps[i], std::vector<const Type *>{t});
                }
                return {t};
              }
            };

        // Could do this as part of the fixity parse if we had support
        // for n-ary operators.
        auto ProductType =
          Separate(AppType, IsToken<TIMES>())
          >[&](const std::vector<const Type *> &v) {
              if (v.size() == 1) return v[0];
              else return pool->Product(v);
            };

        auto FixityElement =
          (IsToken<ARROW>() >>
           Succeed<Token, TypeFixityElt>(ArrowElement)) ||
          (ProductType >[&](const Type *t) {
              TypeFixityElt item;
              item.fixity = Fixity::Atom;
              item.item = t;
              return item;
            });

        return +FixityElement /= ResolveTypeFixity;
      });

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

  const auto RecordPat = [&](const auto &Pattern) {
      return
        ((IsToken<LBRACE>() >>
          Separate0(Label && (IsToken<EQUALS>() >> Pattern),
                    IsToken<COMMA>()) <<
          IsToken<RBRACE>())
         >[&](const std::vector<std::pair<std::string, const Pat *>> &lps) ->
         const Pat * {
             return pool->RecordPat(lps);
           });
    };


  const auto Pattern =
    Fix<Token, const Pat *>([&](const auto &Self) {
        auto AtomicPattern =
          (IsToken<UNDERSCORE>() >[&](auto) { return pool->WildPat(); }) ||
          (BigInteger >[&](const BigInt &i) { return pool->IntPat(i); }) ||
          (StrLit >[&](const std::string &s) {
              return pool->StringPat(s);
            }) ||
          RecordPat(Self) ||
          TuplePat(Self);

        // Allows "x" and "SOME SOME SOME p". Since the head pattern p
        // may itself be an identifier (var pattern), there are
        // several cases to consider.
        auto AppPattern =
          // Must have at least one or the other.
          ((+Id && Opt(AtomicPattern)) ||
           (*Id && (AtomicPattern >[&](const Pat *p) {
               return std::make_optional(p);
             })))
          >[&](const auto &pair) -> const Pat * {
            const auto &[ids, head] = pair;
            CHECK(!ids.empty() || head.has_value());
            if (ids.size() == 1 && !head.has_value()) {
              // This is a simple variable pattern.
              return pool->VarPat(ids[0]);
            }

            std::vector<std::string> spine = ids;
            const Pat *pat = nullptr;
            if (head.has_value()) {
              pat = head.value();
            } else {
              pat = pool->VarPat(spine.back());
              spine.pop_back();
            }

            for (int i = (int)spine.size() - 1;
                 i >= 0;
                 i--) {
              pat = pool->AppPat(spine[i], pat);
            }
            return pat;
          };

        auto AsPattern =
          (AppPattern && Opt(IsToken<AS>() >> Id))
          >[&](const auto &pair) -> const Pat * {
              const auto &[pat, v] = pair;
              if (v.has_value()) {
                return pool->AsPat(pat, v.value());
              } else {
                return pat;
              }
            };

        return (AsPattern && Opt(IsToken<COLON>() >> TypeExpr))
          >[&](const auto &pair) -> const Pat * {
              const auto &[pat, typ] = pair;
              if (typ.has_value()) {
                return pool->AnnPat(pat, typ.value());
              } else {
                return pat;
              }
            };
      });


  // Expressions.

  const auto IntExpr = BigInteger >[&](const BigInt &i) {
      return pool->Int(i);
    };
  const auto FloatExpr = Float >[&](double d) { return pool->Float(d); };
  const auto StrLitExpr = StrLit >[&](const std::string &s) {
      return pool->String(s);
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

  const auto RecordExpr = [&](const auto &Expr) {
      return
        ((IsToken<LBRACE>() >>
          Separate0(Label && (IsToken<EQUALS>() >> Expr),
                    IsToken<COMMA>()) <<
          IsToken<RBRACE>())
         >[&](const std::vector<std::pair<std::string, const Exp *>> &les) {
             return pool->Record(les);
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

  const auto FnExpr = [&](const auto &Expr) {
      return
        ((IsToken<FN>() >> Opt(IsToken<AS>() >> Id)) &&
         (Pattern &&
          (IsToken<DARROW>() >> Expr)))
        >[&](const auto &pp) {
            const auto &[aso, pb] = pp;
            const auto &[pat, body] = pb;
            std::string self = aso.has_value() ? aso.value() : "";
            return pool->Fn(self, pat, body);
          };
    };

  const auto CaseExpr = [&](const auto &Expr) {
      return
        ((IsToken<CASE>() >> Expr << IsToken<OF>()) &&
         (Opt(IsToken<BAR>()) >>
          Separate(
              Pattern && (IsToken<DARROW>() >> Expr),
              IsToken<BAR>())))
        >[&](const auto &pair) {
            const auto &[obj, clauses] = pair;
            return pool->Case(obj, clauses);
          };
    };

  // TODO: For purposes of the value restriction, it would be good if
  // we could mark this internally as a total function.
  const auto ProjectExpr =
    // #1/3 is syntactic sugar for (fn (x, _, _) => x)
    ((IsToken<HASH>() >> Int) && (IsToken<SLASH>() >> Int))
    >[&](const auto &pair) {
        const auto &[lab, num] = pair;
        CHECK(lab > 0 && num > 0 && lab <= num) << "In the syntactic "
          "sugar #l/n, l must be a numeric label that's in range "
          "for a tuple with n elements. Got: " << lab << "/" << num;

        std::string v = "x";
        std::vector<const Pat *> args;
        args.reserve(num);
        for (int i = 0; i < num; i++) {
          if (i + 1 == lab) {
            args.push_back(pool->VarPat(v));
          } else {
            args.push_back(pool->WildPat());
          }
        }
        return pool->Fn(
            // Not recursive
            "",
            pool->TuplePat(args),
            pool->Var(v));
      };

  // Declarations.

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

  // This is like "Nil" or "Cons of a * list".
  // We use null for the type in the first case.
  const auto DatatypeArm =
    (Id && Opt(IsToken<OF>() >> TypeExpr))
    >[&](const auto &p) {
        const auto &[name, otype] = p;
        return std::pair(name, otype.has_value() ? otype.value() : nullptr);
      };

  const auto DatatypeDecl =
    (IsToken<DATATYPE>() >>
     (Opt(IsToken<LPAREN>() >> Separate(Id, IsToken<COMMA>()) <<
          IsToken<RPAREN>()) &&
      Separate(
          Id && (IsToken<EQUALS>() >> Separate(DatatypeArm, IsToken<BAR>())),
          IsToken<AND>())))
    >[&](const auto &p) {
        const auto &[otyvars, dts] = p;
        // Treat absent as empty list.
        std::vector<std::string> tyvars;
        if (otyvars.has_value()) tyvars = otyvars.value();

        std::vector<DatatypeDec> datadecs;
        datadecs.reserve(dts.size());
        for (const auto &[name, arms] : dts) {
          DatatypeDec dd;
          dd.name = name;
          dd.arms = arms;
          datadecs.push_back(dd);
        }
        return pool->DatatypeDec(std::move(tyvars), datadecs);
      };

  const auto AdjApp = [&](const Exp *f, const Exp *arg) -> const Exp * {
      return pool->App(f, arg);
    };

  // TODO: This currently just parses a sequence of function
  // applications in a convoluted way. Need to also pass
  // operators with fixity information in this list!
  using FixityElt = FixityItem<const Exp *>;
  const auto ResolveExprFixity = [&](const std::vector<FixityElt> &elts) ->
    std::optional<const Exp *> {
      // We'd get the same result from the code below, but with
      // more work in this very common case.
    if (elts.size() == 1) {
      CHECK(elts[0].fixity == Fixity::Atom) << FixityString(elts[0].fixity);
      return elts[0].item;
    }

    std::optional<const Exp *> resolved =
      ResolveFixityAdj<const Exp *>(
          elts, Associativity::Left,
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
            FloatExpr ||
            StrLitExpr ||
            // Includes parenthesized expression.
            TupleExpr(Expr) ||
            RecordExpr(Expr) ||
            LayoutExpr(Expr) ||
            LetExpr(Expr, Decl) ||
            ProjectExpr ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Exp *>();

          // To parse infix ops and function application, like
          // "f x + g y", we parse a series of fixity items,
          // which in this case would be "f", "x", "+", "g", "y",
          // where everything is an "atom" except for "+", which
          // is an infix operator.
          auto FixityElement =
            (Id >[&](const std::string &v) {
                // TODO: Should be able to change this in the
                // input source.
                const auto [fixity, assoc, prec] = GetFixity(v);
                FixityElt item;
                item.fixity = fixity;
                item.assoc = assoc;
                item.precedence = prec;
                // A symbol can only be a var or a binary infix operator.
                if (fixity == Fixity::Atom) {
                  item.item = pool->Var(v);
                } else {
                  CHECK(fixity == Fixity::Infix);
                  item.binop = [&, v](const Exp *a, const Exp *b) {
                      return pool->App(pool->Var(v),
                                       pool->Tuple({a, b}));
                    };
                }
                return item;
              }) ||
            (AtomicExpr
             >[&](const Exp *e) {
                FixityElt item;
                item.fixity = Fixity::Atom;
                item.item = e;
                return item;
               });

          auto AppExpr =
            +FixityElement /= ResolveExprFixity;

          // XXX get the precedence of these correct.
          auto AnnotatableExpr = FnExpr(Expr) ||
            IfExpr(Expr) ||
            CaseExpr(Expr) ||
            AppExpr;

          auto AnnExpr =
            (AnnotatableExpr && Opt(IsToken<COLON>() >> TypeExpr))
            >[&](const auto &pair) {
                const auto &[e, ot] = pair;
                if (ot.has_value()) {
                  return pool->Ann(e, ot.value());
                } else {
                  return e;
                }
              };

          return AnnExpr;
        },
        [&](const auto &Expr, const auto &Decl) {
          // Declaration parser.
          return
            DoDecl(Expr) ||
            ValDecl(Expr) ||
            FunDecl(Expr) ||
            DatatypeDecl ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Dec *>();
        });

  auto Program = Expr << End<Token>();

  auto parseopt = Program(std::span<const Token>(tokens.data(),
                                                 tokens.size()));
  CHECK(parseopt.HasValue()) << "Could not parse program.";
  return parseopt.Value();
}

}  // namespace el
