
#include "parsing.h"

#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "el.h"
#include "inclusion.h"
#include "lex.h"
#include "parser-combinators.h"
#include "util.h"
#include "ansi.h"

static constexpr bool VERBOSE = false;

namespace el {

template<TokenType t>
struct IsToken {
  using token_type = Token;
  using out_type = Token;
  constexpr IsToken() {}
  Parsed<Token> operator()(TokenSpan<Token> toks) const {
    if (toks.empty()) return Parsed<Token>::None();
    if (toks[0].type == t) return Parsed(toks[0], 1);
    else return Parsed<Token>::None();
  }
};

// For built-in identifiers, get their fixity, associativity, and precedence.
// TODO: Make it possible to declare new fixity.
[[maybe_unused]]
static std::tuple<Fixity, Associativity, int>
GetFixity(const std::string &sym) {
  static const std::unordered_map<std::string,
                                  std::tuple<Fixity, Associativity, int>>
    builtin = {
    {":=", {Fixity::Infix, Associativity::Non, 2}},

    {"@", {Fixity::Infix, Associativity::Right, 3}},
    {"::", {Fixity::Infix, Associativity::Right, 3}},

    {"o", {Fixity::Infix, Associativity::Left, 4}},

    // TODO: Missing stuff here like ^

    {"==", {Fixity::Infix, Associativity::Non, 5}},
    {"!=", {Fixity::Infix, Associativity::Non, 5}},
    {"==.", {Fixity::Infix, Associativity::Non, 5}},
    {"!=.", {Fixity::Infix, Associativity::Non, 5}},
    {"<", {Fixity::Infix, Associativity::Non, 6}},
    {"<=", {Fixity::Infix, Associativity::Non, 6}},
    {">", {Fixity::Infix, Associativity::Non, 6}},
    {">=", {Fixity::Infix, Associativity::Non, 6}},
    {"<.", {Fixity::Infix, Associativity::Non, 6}},
    {"<=.", {Fixity::Infix, Associativity::Non, 6}},
    {">.", {Fixity::Infix, Associativity::Non, 6}},
    {">=.", {Fixity::Infix, Associativity::Non, 6}},

    {"+", {Fixity::Infix, Associativity::Left, 7}},
    {"-", {Fixity::Infix, Associativity::Left, 7}},
    {"+.", {Fixity::Infix, Associativity::Left, 7}},
    {"-.", {Fixity::Infix, Associativity::Left, 7}},
    {"*", {Fixity::Infix, Associativity::Left, 9}},
    {"*.", {Fixity::Infix, Associativity::Left, 9}},
    {"/", {Fixity::Infix, Associativity::Left, 9}},
    {"/.", {Fixity::Infix, Associativity::Left, 9}},
    {"div", {Fixity::Infix, Associativity::Left, 9}},
    {"mod", {Fixity::Infix, Associativity::Left, 9}},

    // some nonfix symbolic identifiers
    {"!", {Fixity::Atom, Associativity::Non, 0}},
    {"~", {Fixity::Atom, Associativity::Non, 0}},

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
                          const SourceMap &source_map,
                          const std::string &input,
                          const std::vector<Token> &tokens) {

  auto ErrorAtToken = [&source_map](const Token &token) {
    size_t byte_pos = token.start;
    const std::string file = source_map.filecover[byte_pos];
    const int line = source_map.linecover[byte_pos];
    // TODO: Compute the line in the file.
    return StringPrintf(
        "\nAt byte %d which is "
        AWHITE("%s") ":" AYELLOW("%d") ".\n",
        byte_pos, file.c_str(), line);
  };

  // Given as a range of token indices.
  auto ErrorAtIndex = [&ErrorAtToken, &tokens](size_t start, size_t length) {
      if (start >= tokens.size()) {
        return StringPrintf(ARED("token pos %d out of range!"),
                            (int)start);
      } else {
        return ErrorAtToken(tokens[start]);
      }
    };

  auto TokenStr = [&input](Token t) {
      CHECK(t.start <= input.size());
      CHECK(t.start + t.length <= input.size());
      return std::string(input.substr(t.start, t.length));
    };

  const auto Int = IsToken<DIGITS>() >[&](Token t) {
      std::string s = TokenStr(t);
      int64_t i = std::stoll(s);
      CHECK(StringPrintf("%lld", i) == s) << ErrorAtToken(t) <<
        "Invalid integer literal " << s;
      return i;
    };

  auto IntLiteral =
    (IsToken<DIGITS>() >[&](Token t) {
        return TokenStr(t);
      }) ||
    (IsToken<NUMERIC_LIT>() >[&](Token t) {
        return TokenStr(t);
      });

  const auto BigInteger = IntLiteral >[&](const std::string &s) {
      if (s.size() > 1 && s[0] == '0' && (s[1] < '0' || s[1] > '9')) {
        LOG(FATAL) << "unimplemented: Numeric literals of the form "
                   << s;
      }
      return BigInt(s);
    };

  const auto Float = IsToken<FLOAT_LIT>() >[&](Token t) -> double {
      std::string s = TokenStr(t);
      std::optional<double> od = Util::ParseDoubleOpt(s);
      if (od.has_value()) {
        return od.value();
      } else {
        LOG(FATAL) << ErrorAtToken(t) << "Illegal float literal: " << s;
        return 0.0;
      }
    };

  const auto Bool =
    (IsToken<TRUE>() >> Succeed<Token, bool>(true)) ||
    (IsToken<FALSE>() >> Succeed<Token, bool>(false));

  // Use IdAny for expressions, which adds * and others.
  const auto IdType = IsToken<ID>() >[&](Token t) { return TokenStr(t); };
  // Labels can also be numeric. Since they also appear in types (or
  // nearby like in the #1/5 construct), we only allow type
  // identifiers.
  const auto Label = IdType || (IsToken<DIGITS>() >[&](Token t) {
      return TokenStr(t);
    });

  // Strings that can be identifiers outside of the type language.
  auto Id = IdType ||
    (IsToken<TIMES>() >[&](auto&&) -> std::string { return "*"; }) ||
    (IsToken<SLASH>() >[&](auto&&) -> std::string { return "/"; });

  const auto StrLit = IsToken<STR_LIT>() >[&](Token t) {
      // Remove leading and trailing double quotes. Process escapes.
      std::string s = TokenStr(t);
      CHECK(s.size() >= 2) << "Bug: The double quotes are included "
        "in the token.";
      return Lexing::UnescapeStrLit(s.substr(1, s.size() - 2));
    };

  const auto LayoutLit = IsToken<LAYOUT_LIT>() >[&](Token t) {
      return Lexing::UnescapeLayoutLit(TokenStr(t));
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
          IdType >[&](const std::string &s) { return pool->VarType(s, {}); };

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
          (AppArg && *IdType) /=[&](const auto &pair) ->
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

  // In record or objects, we allow writing just "lab" to mean "lab = lab".
  const auto MaybeLabelPun =
    [pool](const std::pair<std::string, std::optional<const Pat *>> &p) {
      const auto &[lab, opat] = p;
      if (opat.has_value()) {
        // XXX check that this is a valid identifier!
        // we should not allow {1}.
        return std::make_pair(lab, opat.value());
      } else {
        return std::make_pair(lab, pool->VarPat(lab));
      }
    };

  const auto RecordPatContents = [&](const auto &Pattern) {
      // lab = pat
      // or
      // lab
      // (which is syntactic sugar for lab = lab).
      auto OneField = (Label && Opt(IsToken<EQUALS>() >> Pattern))
         >MaybeLabelPun;

      return Separate0(OneField, IsToken<COMMA>())
        >[&](const std::vector<std::pair<std::string, const Pat *>> &lps) ->
        const Pat * {
            return pool->RecordPat(lps);
          };
    };

  const auto ObjectPatContents = [&](const auto &Pattern) {
      // Like above, but object labels have to be proper identifiers.
      auto OneField = (Id && Opt(IsToken<EQUALS>() >> Pattern))
         >MaybeLabelPun;

      return
        ((IsToken<LPAREN>() >> Id << IsToken<RPAREN>()) &&
        Separate0(OneField, IsToken<COMMA>()))
        >[&](const auto &p) -> const Pat * {
            const auto &[objtype, lps] = p;
            return pool->ObjectPat(objtype, lps);
          };
    };

  const auto BracedPat = [&](const auto &Pattern) {
      return
        (IsToken<LBRACE>() >>
         (ObjectPatContents(Pattern) || RecordPatContents(Pattern)) <<
         IsToken<RBRACE>());
    };

  const auto PatAdjApp = [&](const Pat *f, const Pat *arg) -> const Pat * {
      CHECK(f->type == PatType::VAR) << "Bug: Only a constructor (identifier) "
        "can be applied in an app pattern. But got: " << PatString(f);
      return pool->AppPat(f->str, arg);
    };

  using PatFixityElt = FixityItem<const Pat *>;
  const auto ResolvePatFixity = [&](const std::vector<PatFixityElt> &elts) ->
    std::optional<const Pat *> {
    if (elts.size() == 1) {
      CHECK(elts[0].fixity == Fixity::Atom) << FixityString(elts[0].fixity);
      return elts[0].item;
    }

    std::optional<const Pat *> resolved =
      ResolveFixityAdj<const Pat *>(
          // ... but we use right associative application, since
          // (SOME SOME) x is useless.
          elts, Associativity::Right,
          std::function<const Pat *(const Pat *, const Pat *)>(PatAdjApp),
          nullptr);

    return resolved;
  };


  // Need to also expose AtomicPattern, since "fun f p1 p2 p3 = ..."
  // uses it.
  const auto &[AtomicPattern, Pattern] =
    Fix2<Token, const Pat *, const Pat *>(

        [&](const auto &AtomicSelf, const auto &Self) {
          // Atomic patterns.
          return
            (IsToken<UNDERSCORE>() >[&](auto) { return pool->WildPat(); }) ||
            (BigInteger >[&](const BigInt &i) { return pool->IntPat(i); }) ||
            (Bool >[&](bool b) { return pool->BoolPat(b); }) ||
            (StrLit >[&](const std::string &s) {
                return pool->StringPat(s);
              }) ||
            BracedPat(Self) ||
            TuplePat(Self);
        },

        [&](const auto &AtomicSelf, const auto &Self) {
          // Full patterns.

          // This is just like parsing fixity expressions. The elements
          // are either identifiers (with the appropriate fixity) or
          // atomic patterns (atoms).
          auto FixityElement =
            (Id >[&](const std::string &v) {
                const auto [fixity, assoc, prec] = GetFixity(v);
                PatFixityElt item;
                item.fixity = fixity;
                item.assoc = assoc;
                item.precedence = prec;
                // A symbol can only be a var or a binary infix operator.
                if (fixity == Fixity::Atom) {
                  item.item = pool->VarPat(v);
                } else {
                  CHECK(fixity == Fixity::Infix);
                  item.binop = [&, v](const Pat *a, const Pat *b) {
                      return pool->AppPat(v,
                                          pool->TuplePat({a, b}));
                    };
                }
                return item;
              }) ||
            (AtomicSelf
             >[&](const Pat *p) {
                PatFixityElt item;
                item.fixity = Fixity::Atom;
                item.item = p;
                return item;
               });

          auto AppPattern =
            +FixityElement /= ResolvePatFixity;

          // Unlike sml, we allow "p1 as p2".
          auto AsPattern =
            (AppPattern && Opt(IsToken<AS>() >> AppPattern))
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

  // Because AppPattern is actually where var patterns are parsed (as
  // the singleton instance of app patterns), we need to re-add that
  // case to atomic patterns for fun decls.
  auto CurryPattern = AtomicPattern ||
    (Id >[&](const std::string &v) { return pool->VarPat(v); });

  // Expressions.

  const auto IntExpr = BigInteger >[&](const BigInt &i) {
      return pool->Int(i);
    };
  const auto FloatExpr = Float >[&](double d) { return pool->Float(d); };
  const auto StrLitExpr = StrLit >[&](const std::string &s) {
      return pool->String(s);
    };
  const auto BoolExpr = Bool >[&](bool b) { return pool->Bool(b); };

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

  // contents inside { ... }
  const auto RecordContents = [&](const auto &Expr) {
      // Allow just { x } ?
      return
        Separate0(Label && (IsToken<EQUALS>() >> Expr),
                  IsToken<COMMA>())
        >[&](const std::vector<std::pair<std::string, const Exp *>> &les) {
            return pool->Record(les);
          };
    };

  // other contents inside { ... }
  const auto ObjectContents = [&](const auto &Expr) {
      return
        ((IsToken<LPAREN>() >> Id << IsToken<RPAREN>()) &&
         // Not using Label here, since we may have some ambiguity
         // in like "obj.3".
         Separate0(Id && (IsToken<EQUALS>() >> Expr),
                   IsToken<COMMA>()))
        >[&](const auto &p) {
            const auto &[objtype, fields] = p;
            return pool->Object(objtype, fields);
          };
    };

  const auto BracedExpr = [&](const auto &Expr) {
    return IsToken<LBRACE>() >>
      // XXX why doesn't it work to reverse these two?
      (ObjectContents(Expr) || RecordContents(Expr))
      << IsToken<RBRACE>();
  };

  const auto LayoutExpr = [&](const auto &Expr) {
      const auto Lay =
        Fix<Token, const Layout *>([&](const auto &Self) {
            // This is the contents of [brackets] inside a layout
            // expression. It can either be a regular expression,
            // or a [* layout comment *] token. In the latter case,
            // we return nullptr and ignore it in the Join.
            auto Nested =
              Expr ||
              (IsToken<LAYOUT_COMMENT>() >>
               Succeed<Token, const Exp *>(nullptr));

            return (LayoutLit &&
              *((IsToken<LBRACKET>() >> Nested << IsToken<RBRACKET>()) &&
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
                      // For layout comments, there is no expression.
                      if (e != nullptr) {
                        joinme.push_back(pool->ExpLayout(e));
                      }
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
              // Trailing semicolon is allowed, unlike SML.
              (Separate(Expr, IsToken<SEMICOLON>()) <<
               Opt(IsToken<SEMICOLON>())) <<
              IsToken<END>())
             >[&](const auto &p) {
                  const auto &[ds, es] = p;
                  CHECK(!es.empty()) << "Impossible. Separate must "
                    "parse at least one exp!";
                  std::vector<const Dec *> decs = ds;
                  // parse any leading sequence expressions as
                  // val _ = e  (but the last one is the body.)
                  for (int i = 0; i < (int)es.size() - 1; i++) {
                    decs.push_back(pool->ValDec(pool->WildPat(), es[i]));
                  }

                  return pool->Let(std::move(decs), es.back());
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
         Separate(Pattern && (IsToken<DARROW>() >> Expr),
                  IsToken<BAR>()))
        >[&](const auto &pp) {
            const auto &[aso, clauses] = pp;
            std::string self = aso.has_value() ? aso.value() : "";
            return pool->Fn(self, clauses);
          };
    };

  const auto CaseExpr = [&](const auto &Expr) {
      return
        (((IsToken<CASE>() >> Expr << IsToken<OF>()) &&
         (Opt(IsToken<BAR>()) >>
          Separate(
              Pattern && (IsToken<DARROW>() >> Expr),
              IsToken<BAR>())))
        >[&](const auto &pair) {
            const auto &[obj, clauses] = pair;
            return pool->Case(obj, clauses);
          }) ||
        (Mark(IsToken<CASE>()) >[&](const auto &err) -> const Exp * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected CASE EXP OF ROWS+ after seeing "
              "CASE. At: " << start << " for " << length;
            return nullptr;
          });
    };

  const auto FailExpr = [&](const auto &Expr) {
      return (IsToken<FAIL>() >> Expr)
        >[&](const Exp *msg) {
            return pool->Fail(msg);
          };
    };

  // TODO: For purposes of the value restriction, it would be good if
  // we could mark this internally as a total function.
  const auto ProjectExpr =
    // #1/3 is syntactic sugar for (fn (x, _, _) => x)
    ((IsToken<HASH>() >> Int) && (IsToken<SLASH>() >> Int))
    >[&](const auto &pair) -> const Exp * {
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
            {{pool->TuplePat(args), pool->Var(v)}});
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
      return (((IsToken<VAL>() >> Pattern) &&
              (IsToken<EQUALS>() >> Expr))
        >[&](const auto &p) {
            return pool->ValDec(p.first, p.second);
          }) ||
        (Mark(IsToken<VAL>()) >[&](const auto &err) -> const Dec * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected VAL PAT EQUALS EXP after seeing "
              "VAL. At: " << start << " for " << length;
            return nullptr;
          });
    };

  const auto OneFunDec = [&](const auto &clauses) -> FunDec {
      CHECK(!clauses.empty()) << "Bug: shouldn't even parse "
        "empty fun clauses!";
      const std::string &fname =
        std::get<0>(std::get<0>(std::get<0>(clauses[0])));
      // printf("OneFunDec: %s\n", fname.c_str());
      FunDec ret;
      ret.name = fname;
      for (const auto &ppte : clauses) {
        const auto &[ppt, e] = ppte;
        const auto &[pp, to] = ppt;
        const auto &[id, curry_pats] = pp;
        CHECK(id == fname) << "Inconsistent name for function in "
          "function clauses: Saw " << fname << " and then " << id;
        CHECK(!curry_pats.empty()) << "Shouldn't parse empty curry "
          "patterns!";

        // trailing annotation is syntactic sugar for putting
        // the annotation on the expression.
        const Exp *exp = e;
        if (to.has_value()) {
          exp = pool->Ann(exp, to.value());
        }
        ret.clauses.emplace_back(curry_pats, exp);
      }
      return ret;
  };

  // fun f p11 p12 p13 : ann1 = e1
  //   | f p21 p22 p23 : ann2 = e2
  //   | ...
  // and g q1 = ee1
  //   | g q2 = ee2
  //   | ...
  const auto FunDecl = [&](const auto &Expr) {
      auto row =
        (Separate(Id && +CurryPattern &&
                  // optional type annotation
                  Opt(IsToken<COLON>() >> TypeExpr) &&
                  (IsToken<EQUALS>() >> Expr),
                  IsToken<BAR>()))
        >OneFunDec;

      return
        (((IsToken<FUN>() >> row) &&
         *(IsToken<AND>() >> row))
        >[&](const auto &p) {
            const FunDec &f = p.first;
            const std::vector<FunDec> &fs = p.second;
            if (VERBOSE) { printf("Fundec %s + %d\n", f.name.c_str(),
                                  (int)fs.size()); }
            std::vector<FunDec> funs = {f};
            for (const FunDec &d : fs) funs.push_back(d);
            return pool->FunDec(std::move(funs));
          }) ||
        (Mark(IsToken<FUN>()) >[&](const auto &err) -> const Dec * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected FUN ID PAT+ EQUALS EXP after seeing "
              "FUN. At: " << start << " for " << length;
            return nullptr;
          });
    };

  // object Article of { title : string, year : int }
  // We allow parsing any type, but only accept base types in elaboration.
  const auto ObjectDecl =
    (((IsToken<OBJECT>() >> Id << IsToken<OF>() << IsToken<LBRACE>()) &&
      (Separate0(Id && (IsToken<COLON>() >> TypeExpr),
                 IsToken<COMMA>()) << IsToken<RBRACE>()
       ))
     >[&](const auto &p) {
         const auto &[name, fields] = p;
         return pool->ObjectDec(ObjectDec({.name = name, .fields = fields}));
       }) ||
    (Mark(IsToken<OBJECT>()) >[&](const auto &err) -> const Dec * {
        const auto &[_, start, length] = err;
        LOG(FATAL) << ErrorAtIndex(start, length) <<
          "Expected OBJECT ID OF LBRACE ... after seeing "
          "OBJECT. At: " << start << " for " << length;
        return nullptr;
      });


  // This is like "Nil" or "Cons of a * list".
  // We use null for the type in the first case.
  const auto DatatypeArm =
    (Id && Opt(IsToken<OF>() >> TypeExpr))
    >[&](const auto &p) {
        const auto &[name, otype] = p;
        return std::pair(name, otype.has_value() ? otype.value() : nullptr);
      };

  const auto DatatypeDecl =
    ((IsToken<DATATYPE>() >>
     // Type variables appear in the type language, so don't allow
     // non-type identifiers.
     (Opt(IsToken<LPAREN>() >> Separate(IdType, IsToken<COMMA>()) <<
          IsToken<RPAREN>()) &&
      Separate(
          IdType && (IsToken<EQUALS>() >>
                     Separate(DatatypeArm, IsToken<BAR>())),
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
      }) ||
    (Mark(IsToken<DATATYPE>()) >[&](const auto &err) -> const Dec * {
        const auto &[_, start, length] = err;
        LOG(FATAL) << ErrorAtIndex(start, length) <<
          "Expected DATATYPE TYVARS* ID EQUALS ... after seeing "
          "DATATYPE. At: " << start << " for " << length;
        return nullptr;
      });


  const auto ExpAdjApp = [&](const Exp *f, const Exp *arg) -> const Exp * {
      return pool->App(f, arg);
    };

  using ExpFixityElt = FixityItem<const Exp *>;
  const auto ResolveExprFixity = [&](const std::vector<ExpFixityElt> &elts) ->
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
          std::function<const Exp *(const Exp *, const Exp *)>(ExpAdjApp),
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
            BoolExpr ||
            // Includes parenthesized expression.
            TupleExpr(Expr) ||
            BracedExpr(Expr) ||
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
                ExpFixityElt item;
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
                ExpFixityElt item;
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
            FailExpr(Expr) ||
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

          // These should probably be in AnnotatableExpr?
          auto AndalsoExpr =
            (AnnExpr && Opt((IsToken<ANDALSO>() || IsToken<ANDTHEN>()) &&
                            AnnExpr))
            >[&](const auto &pair) {
                const auto &[e, oand] = pair;
                if (oand.has_value()) {
                  const auto &[kw, rhs] = oand.value();
                  if (kw.type == ANDALSO) {
                    return pool->Andalso(e, rhs);
                  } else {
                    // This is treated as syntactic sugar, which isn't
                    // ideal.
                    return pool->If(e, rhs, pool->Tuple({}));
                  }
                } else {
                  return e;
                }
              };

          auto OrelseExpr =
            (AndalsoExpr && Opt((IsToken<ORELSE>() || IsToken<OTHERWISE>()) &&
                                AndalsoExpr))
            >[&](const auto &pair) {
                const auto &[e, oor] = pair;
                if (oor.has_value()) {
                  const auto &[kw, rhs] = oor.value();
                  if (kw.type == ORELSE) {
                    return pool->Orelse(e, rhs);
                  } else {
                    return pool->If(e, pool->Tuple({}), rhs);
                  }
                } else {
                  return e;
                }
              };

          auto WithoutExpr =
            (OrelseExpr && *(IsToken<WITHOUT>() >>
                             (IsToken<LPAREN>() >> Id << IsToken<RPAREN>()) &&
                             Id))
            >[&](const auto &pair) -> const Exp * {
                const auto &[e, withouts] = pair;
                const Exp *ret = e;
                for (const auto &[objname, field] : withouts) {
                  ret = pool->Without(ret, objname, field);
                }
                return ret;
              };

          auto WithExpr =
            (WithoutExpr && *(IsToken<WITH>() >>
                             Opt(IsToken<LPAREN>() >> Id << IsToken<RPAREN>()) &&
                             Id &&
                             (IsToken<EQUALS>() >> WithoutExpr)))
            >[&](const auto &pair) -> const Exp * {
                const auto &[e, withs] = pair;
                const Exp *ret = e;
                for (const auto &[p1, e2] : withs) {
                  const auto &[oobjname, field] = p1;
                  ret = pool->With(ret,
                                   oobjname.has_value() ? oobjname.value() : "",
                                   field,
                                   e2);
                }
                return ret;
              };

          return WithExpr;
        },
        [&](const auto &Expr, const auto &Decl) {
          // Declaration parser.
          return
            DoDecl(Expr) ||
            ValDecl(Expr) ||
            FunDecl(Expr) ||
            DatatypeDecl ||
            ObjectDecl ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Dec *>();
        });

  auto Program = Expr << End<Token>();

  auto parseopt = Program(TokenSpan<Token>(tokens.data(), tokens.size()));
  CHECK(parseopt.HasValue()) << "Could not parse program.";
  return parseopt.Value();
}

}  // namespace el
