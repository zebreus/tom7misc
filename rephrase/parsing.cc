
#include "parsing.h"

#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"
#include "el.h"
#include "inclusion.h"
#include "lexing.h"
#include "parser-combinators.h"
#include "utf8.h"
#include "util.h"

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

    {"shl", {Fixity::Infix, Associativity::Non, 10}},
    {"shr", {Fixity::Infix, Associativity::Non, 10}},

    {"andb", {Fixity::Infix, Associativity::Left, 11}},
    {"xorb", {Fixity::Infix, Associativity::Left, 12}},
    {"orb", {Fixity::Infix, Associativity::Left, 13}},

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
    return std::format(
        "\nAt byte {} which is "
        AWHITE("{}") ":" AYELLOW("{}") ".\n",
        byte_pos, file, line);
  };

  // Given as a range of token indices.
  auto ErrorAtIndex = [&ErrorAtToken, &tokens](size_t start, size_t length) {
      if (start >= tokens.size()) {
        return std::format(ARED("token pos {} out of range!"), start);
      } else {
        return ErrorAtToken(tokens[start]);
      }
    };

  auto BytePos = [&tokens](size_t token_pos) {
      // Print("BytePos: {}\n", token_pos);
      CHECK(token_pos < tokens.size()) << token_pos << " vs " << tokens.size();
      return tokens[token_pos].start;
    };

  auto TokenStr = [&input](Token t) {
      CHECK(t.start <= input.size());
      CHECK(t.start + t.length <= input.size());
      return std::string(input.substr(t.start, t.length));
    };

  const auto Int = IsToken<DIGITS>() >[&](Token t) {
      std::string s = TokenStr(t);
      int64_t i = std::stoll(s);
      CHECK(std::format("{}", i) == s) << ErrorAtToken(t) <<
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

  const auto BigInteger = IntLiteral >[&](const std::string &s_in) {
      // . is ignored in this type of literal. We rely on lexing
      // to distinguish a floating point literal.
      std::string s = Util::RemoveChar(s_in, '.');
      if (s.size() > 1 && s[0] == '0') {
        // Handle 0x, 0b, etc.
        const char r = s[1];
        if (r >= '0' && r <= '9') {
          // This is just a regular integer with a leading zero.
          // We do not treat 0777 etc. as octal. Use 0o for that.
          return BigInt(s);

        } if (r == 'D' || r == 'd') {
          return BigInt(s.substr(2, std::string::npos));

        } if (r == 'X' || r == 'x') {
          // Hex literal.
          BigInt b(0);
          for (int i = 2; i < (int)s.size(); i++) {
            char c = s[i];
            b = BigInt::LeftShift(std::move(b), 4);
            BigInt::PlusEq(b, BigInt(Util::HexDigitValue(c)));
          }
          return b;

        } if (r == 'B' || r == 'b') {
          // Binary literal.
          BigInt b(0);
          for (int i = 2; i < (int)s.size(); i++) {
            char c = s[i];
            b = BigInt::LeftShift(std::move(b), 1);
            if (c == '1') {
              BigInt::PlusEq(b, BigInt(1));
            }
          }
          return b;

        } if (r == 'O' || r == 'o') {
          // Octal literal.
          BigInt b(0);
          for (int i = 2; i < (int)s.size(); i++) {
            char c = s[i];
            b = BigInt::LeftShift(std::move(b), 3);
            BigInt::PlusEq(b, BigInt(Util::HexDigitValue(c)));
          }
          return b;

        } if (r == '\'') {
          // Codepoint literal.

          std::string_view ss(s);
          ss.remove_prefix(2);
          std::vector<uint32_t> cps = UTF8::Codepoints(ss);
          // Including the closing '
          CHECK(cps.size() == 2 && cps[1] == '\'') <<
            "Invalid codepoint literal " <<
            s << ". It must be UTF8 that decodes to exactly one "
            "codepoint, but got " << cps.size() - 1;

          return BigInt(cps[0]);

        } else {
          LOG(FATAL) << "unimplemented: Numeric literals of the form "
                     << s;
        }
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

  // Use Id for expressions, which adds * and others.
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
    .item = nullptr,
    .unop = nullptr,
    .binop = [&](const Type *a, const Type *b) {
        return pool->Arrow(a, b);
      },
  };

  const auto TypeExprBase =
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
          (Mark(IdType)
           >[&](const auto &s_pos) -> const Type * {
            const auto &[s, token_start, token_len] = s_pos;
            const size_t pos = BytePos(token_start);
            return pool->VarType(s, {}, pos);
          });

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
                const Type *t = pool->VarType(vapps[0], varg,
                                              SourceMap::BOGUS_POS);
                for (int i = 1; i < (int)vapps.size(); i++) {
                  t = pool->VarType(vapps[i], std::vector<const Type *>{t},
                                    SourceMap::BOGUS_POS);
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

  auto TypeExpr = MemoizedParser(TypeExprBase);

  // Patterns.

  // TODO: support patterns like this!
  // { name: string,
  //   regular: string option,
  //   bold: string option,
  //   italic: string option,
  //   bold-italic: string option }
  const auto TuplePat = [&](const auto &Pattern) {
      return (Mark(IsToken<LPAREN>() >>
                   Separate0(Pattern, IsToken<COMMA>()) <<
                   IsToken<RPAREN>())
              >[&](const auto &pats_pos) -> const Pat * {
                  // const std::vector<const Pat *> &ps
                  const auto &[ps, token_start, token_len] = pats_pos;
                  if (ps.size() == 1) {
                    // Then this is just a parenthesized pattern.
                    return ps[0];
                  } else {
                    return pool->TuplePat(ps, BytePos(token_start));
                  }
                });
    };

  // In record or objects, we allow writing just "lab" to mean "lab = lab".
  const auto MaybeLabelPun =
    [pool, &BytePos](const std::tuple<
           std::pair<std::string, std::optional<const Pat *>>,
           size_t,
           size_t> &p_pos) {
      const auto &[p, token_start, token_len] = p_pos;
      const auto &[lab, opat] = p;
      if (opat.has_value()) {
        // XXX check that this is a valid identifier!
        // we should not allow {1}.
        return std::make_pair(lab, opat.value());
      } else {
        return std::make_pair(lab, pool->VarPat(lab, BytePos(token_start)));
      }
    };

  const auto RecordPatContents = [&](const auto &Pattern) {
      // lab = pat
      //   or
      // lab
      //   or
      // lab : type
      // (which is syntactic sugar for lab = lab).
      auto OneField =
        (Mark(Label && (IsToken<COLON>() >> TypeExpr))
         >[&](const auto &p_pos) -> std::pair<std::string, const Pat *> {
          const auto &[p, token_start, token_len] = p_pos;
          const auto &[lab, ty] = p;
          const size_t pos = BytePos(token_start);
          return std::make_pair(lab,
                                pool->AnnPat(
                                    pool->VarPat(lab, pos),
                                    ty,
                                    pos));
        }) ||
        (Mark(Label && Opt(IsToken<EQUALS>() >> Pattern))
         >MaybeLabelPun);

      return Mark(Separate0(OneField, IsToken<COMMA>()))
        >[&](const auto &lps_pos) ->
        const Pat * {
            // const std::vector<std::pair<std::string, const Pat *>> &lps;
            const auto &[lps, token_start, token_len] = lps_pos;
            return pool->RecordPat(lps, BytePos(token_start));
          };
    };

  const auto ObjectPatContents = [&](const auto &Pattern) {
      // Like above, but object labels have to be proper identifiers.
      auto OneField = Mark(Id && Opt(IsToken<EQUALS>() >> Pattern))
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
  const auto &[AtomicPatternBase, PatternBase] =
    MemoizedFix2<Token, const Pat *, const Pat *>(

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
            (Mark(Id) >[&](const auto &v_pos) {
                const auto &[v, token_start, token_len] = v_pos;
                const auto [fixity, assoc, prec] = GetFixity(v);
                PatFixityElt item;
                item.fixity = fixity;
                item.assoc = assoc;
                item.precedence = prec;
                // A symbol can only be a var or a binary infix operator.
                size_t pos = BytePos(token_start);
                if (fixity == Fixity::Atom) {
                  item.item = pool->VarPat(v, pos);
                } else {
                  CHECK(fixity == Fixity::Infix);
                  item.binop = [&, v](const Pat *a, const Pat *b) {
                      return pool->AppPat(v,
                                          pool->TuplePat({a, b}, pos));
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

          return (AsPattern && Opt(Mark(IsToken<COLON>() >> TypeExpr)))
            >[&](const auto &pair) -> const Pat * {
                const auto &[pat, typ_pos] = pair;
                if (typ_pos.has_value()) {
                  const auto &[typ, token_start, token_length] =
                    typ_pos.value();
                  return pool->AnnPat(pat, typ, BytePos(token_start));
                } else {
                  return pat;
                }
              };
        });

  auto AtomicPattern = MemoizedParser(AtomicPatternBase);
  auto Pattern = MemoizedParser(PatternBase);

  // Because AppPattern is actually where var patterns are parsed (as
  // the singleton instance of app patterns), we need to re-add that
  // case to atomic patterns for fun decls.
  auto CurryPattern = AtomicPattern ||
    (Mark(Id) >[&](const auto &v_pos) {
        const auto &[v, token_start, token_len] = v_pos;
        return pool->VarPat(v, BytePos(token_start));
      });

  // Expressions.

  const auto IntExpr = BigInteger >[&](const BigInt &i) {
      return pool->Int(i);
    };
  const auto FloatExpr = Float >[&](double d) { return pool->Float(d); };
  const auto StrLitExpr = Mark(StrLit)
    >[&](const auto &s_pos_len) -> const Exp * {
        const auto &[s, token_pos, token_len] = s_pos_len;
        // Print("StrLitExp: {}\n", token_pos);
        return pool->String(s, BytePos(token_pos));
    };
  const auto BoolExpr = Bool >[&](bool b) { return pool->Bool(b); };

  // Either (), or (e) or (e1, e2, ...).
  const auto TupleExpr = [&](const auto &Expr) {
      return (Mark(IsToken<LPAREN>() >>
               Separate0(Expr, IsToken<COMMA>()) <<
               IsToken<RPAREN>())
              >[&](const auto &v_pos) {
                  const auto &[es, token_start, token_len] = v_pos;
                  if (es.size() == 1) {
                    // Then this is just a parenthesized expression.
                    return es[0];
                  } else {
                    return pool->Tuple(es, BytePos(token_start));
                  }
                });
    };

  // contents inside { ... }
  const auto RecordContents = [&](const auto &Expr) {
      // Allow just { x } ?
      return
        Mark(Separate0(Label && (IsToken<EQUALS>() >> Expr),
                       IsToken<COMMA>()))
        >[&](const auto &les_pos) {
            // const std::vector<std::pair<std::string, const Exp *>> &les
            const auto &[les, token_start, token_end] = les_pos;
            return pool->Record(les, BytePos(token_start));
          };
    };

  // other contents inside { ... }
  const auto ObjectContents = [&](const auto &Expr) {
      return
        Mark((IsToken<LPAREN>() >> Id << IsToken<RPAREN>()) &&
             // Not using Label here, since we may have some ambiguity
             // in like "obj.3".
             Separate0(Id && (IsToken<EQUALS>() >> Expr),
                       IsToken<COMMA>()))
        >[&](const auto &p_pos) {
            const auto &[p, token_start, token_end] = p_pos;
            const auto &[objtype, fields] = p;
            return pool->Object(objtype, fields, BytePos(token_start));
          };
    };

  const auto BracedExpr = [&](const auto &Expr) {
      return (IsToken<LBRACE>() >>
              // XXX why doesn't it work to reverse these two?
              (ObjectContents(Expr) || RecordContents(Expr)) <<
              // Allow and ignore trailing comma.
              Opt(IsToken<COMMA>()) <<
              IsToken<RBRACE>()) ||
    (Mark(IsToken<LBRACE>()) >[&](const auto &err) -> const Exp * {
        const auto &[_, start, length] = err;
        LOG(FATAL) << ErrorAtIndex(start, length) <<
          "Expected [(OBJNAME)] fields... after seeing "
          "LBRACE. At: " << start << " for " << length;
        return nullptr;
      });
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

            auto LayoutParticle =
              (IsToken<LBRACKET>() >> Nested << IsToken<RBRACKET>()) ||

              (Mark(IsToken<LBRACKET>()) >[&](const auto &err) -> const Exp * {
                  const auto &[_, start, length] = err;
                  LOG(FATAL) << ErrorAtIndex(start, length) <<
                    "Expected LBRACKET exp RBRACKET after seeing LBRACKET "
                    "inside layout.\n"
                    "At: " << start << " for " << length;
                  return nullptr;
                });

            return (LayoutLit &&
              *(LayoutParticle &&
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

      return Mark(IsToken<LBRACKET>() >> Lay << IsToken<RBRACKET>())
          >[&](const auto &lay_pos) {
              const auto &[lay, token_start, token_len] = lay_pos;
              return pool->LayoutExp(lay, BytePos(token_start));
            };
    };

  const auto LetExpr = [&](const auto &Expr, const auto &Decl) {
      return (Mark((IsToken<LET>() >> *Decl << IsToken<IN>()) &&
                   // Trailing semicolon is allowed, unlike SML.
                   (Separate(Expr, IsToken<SEMICOLON>()) <<
                    Opt(IsToken<SEMICOLON>())) <<
                   IsToken<END>())
             >[&](const auto &p_pos) {
                 const auto &[p, token_start, token_len] = p_pos;
                 const auto &[ds, es] = p;
                 CHECK(!es.empty()) << "Impossible. Separate must "
                   "parse at least one exp!";
                 std::vector<const Dec *> decs = ds;
                 // parse any leading sequence expressions as
                 // val _ = e  (but the last one is the body.)
                 for (int i = 0; i < (int)es.size() - 1; i++) {
                   decs.push_back(pool->ValDec(pool->WildPat(), es[i]));
                 }

                 return pool->Let(std::move(decs), es.back(),
                                  BytePos(token_start));
               }) ||
        (Mark(IsToken<LET>()) >[&](const auto &err) -> const Exp * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected LET [DECS] IN (EXP;) END ... after seeing "
              "LET. At: " << start << " for " << length;
            return nullptr;
          });
    };

  const auto IfExpr = [&](const auto &Expr) {
      return
        (((IsToken<IF>() >> Expr) &&
         (IsToken<THEN>() >> Expr) &&
         (IsToken<ELSE>() >> Expr))
        >[&](const auto &p) {
            const auto &[pp, f] = p;
            const auto &[cond, t] = pp;
            return pool->If(cond, t, f);
          }) ||

        (Mark(IsToken<IF>()) >[&](const auto &err) -> const Exp * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected IF EXP THEN EXP ELSE EXP after seeing "
              "IF. At: " << start << " for " << length;
            return nullptr;
          });
    };

  const auto FnExpr = [&](const auto &Expr) {
      return
        Mark((IsToken<FN>() >> Opt(IsToken<AS>() >> Id)) &&
             Separate(Pattern && (IsToken<DARROW>() >> Expr),
                      IsToken<BAR>()))
        >[&](const auto &pp_start_len) {
            const auto &[pp, token_start, token_len] = pp_start_len;
            const auto &[aso, clauses] = pp;
            std::string self = aso.has_value() ? aso.value() : "";
            return pool->Fn(self, clauses, BytePos(token_start));
          };
    };

  const auto CaseExpr = [&](const auto &Expr) {
      return
        (Mark((IsToken<CASE>() >> Expr << IsToken<OF>()) &&
              (Opt(IsToken<BAR>()) >>
               Separate(
                   Pattern && (IsToken<DARROW>() >> Expr),
                   IsToken<BAR>())))
        >[&](const auto &pair_start_len) {
            const auto &[pair, token_start, token_len] = pair_start_len;
            const auto &[obj, clauses] = pair;
            size_t byte_pos = BytePos(token_start);
            return pool->Case(obj, clauses, byte_pos);
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
    Mark((IsToken<HASH>() >> Int) && (IsToken<SLASH>() >> Int))
    >[&](const auto &pair_pos) -> const Exp * {
        const auto &[pair, token_start, token_len] = pair_pos;
        const auto &[lab, num] = pair;
        CHECK(lab > 0 && num > 0 && lab <= num) << "In the syntactic "
          "sugar #l/n, l must be a numeric label that's in range "
          "for a tuple with n elements. Got: " << lab << "/" << num;

        size_t byte_pos = BytePos(token_start);
        std::string v = "x";
        std::vector<const Pat *> args;
        args.reserve(num);
        for (int i = 0; i < num; i++) {
          if (i + 1 == lab) {
            args.push_back(pool->VarPat(v, byte_pos));
          } else {
            args.push_back(pool->WildPat());
          }
        }
        // Print("ProjectExpr: {}\n", token_start);

        return pool->Fn(
            // Not recursive
            "",
            {{pool->TuplePat(args, byte_pos), pool->Var(v, byte_pos)}},
            byte_pos);
      };

  // Declarations.

  // This is syntactic sugar for val _ = e
  const auto DoDecl = [&](const auto &Expr) {
      return ((IsToken<DO>() >> Expr)
              >[&](const Exp *e) {
                  return pool->ValDec(pool->WildPat(), e);
                }) ||
        (Mark(IsToken<DO>()) >[&](const auto &err) -> const Dec * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected DO EXP after seeing "
              // TODO: We can explicitly detect (or allow) a semicolon?
              "DO. Extra semicolon? At: " << start << " for " << length;
            return nullptr;
          });
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
      // Print("OneFunDec: {}\n", fname);
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
          // Could get a more accurate position here (from
          // the annotation itself...)
          exp = pool->Ann(exp, to.value(), e->pos);
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
        (Mark((IsToken<FUN>() >> row) &&
              *(IsToken<AND>() >> row))
        >[&](const auto &p_start_len) {
            const auto &[p, token_start, token_len] = p_start_len;
            const FunDec &f = p.first;
            const std::vector<FunDec> &fs = p.second;
            if (VERBOSE) {
              Print("Fundec {} + {}\n", f.name, fs.size());
            }
            std::vector<FunDec> funs = {f};
            for (const FunDec &d : fs) funs.push_back(d);
            return pool->FunDec(std::move(funs), BytePos(token_start));
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

  const auto LocalDecl = [&](const auto &Decl) {
      return (((IsToken<LOCAL>() >> *Decl << IsToken<IN>()) &&
               (*Decl << IsToken<END>()))
              >[&](const auto &p) {
                  const auto &[ds1, ds2] = p;
                  return pool->LocalDec(ds1, ds2);
                }) ||
        (Mark(IsToken<LOCAL>()) >[&](const auto &err) -> const Dec * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected LOCAL [DECS] IN [DECS] END ... after seeing "
              "LOCAL. At: " << start << " for " << length;
            return nullptr;
          });
    };

  auto TypeDecl =
    // TODO: Tyvars; it's easy
    ((IsToken<TYPE>() >> IdType) && (IsToken<EQUALS>() >> TypeExpr))
    >[&](const auto &p) -> const Dec * {
        return pool->TypeDec({}, p.first, p.second);
      };

  auto OpenDecl = [&](const auto &Expr) {
      return ((IsToken<OPEN>() >> Expr)
              >[&](const Exp *e) -> const Dec * {
                  return pool->OpenDec(e);
                }) ||
        (Mark(IsToken<OPEN>()) >[&](const auto &err) -> const Dec * {
            const auto &[_, start, length] = err;
            LOG(FATAL) << ErrorAtIndex(start, length) <<
              "Expected OPEN EXP after seeing "
              "OPEN. At: " << start << " for " << length;
            return nullptr;
          });
    };

  auto ErrorDecl =
    Mark(IsToken<SEMICOLON>()) >[&](const auto &err) -> const Dec * {
        const auto &[tok, start, length] = err;
        LOG(FATAL) << ErrorAtIndex(start, length) <<
          "Expected declaration, but got " <<
          std::format(AORANGE("{}"), TokenTypeString(tok.type)) <<
          "\nAt: " << start << " for " << length;
        return nullptr;
      };

  const auto ExpAdjApp = [&](const Exp *f, const Exp *arg) -> const Exp * {
      // XXX Reusing the argument's position. We could maybe do
      // better, but there is no actual keyword associated with an
      // application.
      return pool->App(f, arg, arg->pos);
    };

  using ExpFixityElt = FixityItem<const Exp *>;
  const auto ResolveExprFixity = [&](const std::vector<ExpFixityElt> &elts) ->
    std::optional<const Exp *> {
      // We'd get the same result from the code below, but with
      // more work in this very common case.
    if (elts.size() == 1) {
      // Possible for user programs to generate this with an expression like
      // [[/]]
      CHECK(elts[0].fixity == Fixity::Atom) << FixityString(elts[0].fixity)
      << "Saw an infix operator on its own?";

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
    MemoizedFix2<Token, const Exp *, const Dec *>(
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
            (Mark(Id) >[&](const auto &v_pos) {
                const auto &[v, token_start, token_len] = v_pos;
                // TODO: Should be able to change this in the
                // input source.
                const auto [fixity, assoc, prec] = GetFixity(v);
                ExpFixityElt item;
                item.fixity = fixity;
                item.assoc = assoc;
                item.precedence = prec;
                // A symbol can only be a var or a binary infix operator.
                size_t byte_pos = BytePos(token_start);
                if (fixity == Fixity::Atom) {
                  item.item = pool->Var(v, byte_pos);
                } else {
                  CHECK(fixity == Fixity::Infix);
                  item.binop = [pool, byte_pos, v](const Exp *a, const Exp *b) {
                      return pool->App(pool->Var(v, byte_pos),
                                       pool->Tuple({a, b}, byte_pos),
                                       byte_pos);
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
            (AnnotatableExpr && Opt(Mark(IsToken<COLON>() >> TypeExpr)))
            >[&](const auto &pair) {
                const auto &[e, ot_pos] = pair;
                if (ot_pos.has_value()) {
                  const auto &[t, token_start, token_end] = ot_pos.value();
                  return pool->Ann(e, t, BytePos(token_start));
                } else {
                  return e;
                }
              };

          // These should probably be in AnnotatableExpr?
          auto AndalsoExpr =
            Mark(AnnExpr && Opt((IsToken<ANDALSO>() || IsToken<ANDTHEN>()) &&
                                AnnExpr))
            >[&](const auto &pair_pos) {
                const auto &[pair, token_start, token_len] = pair_pos;
                const auto &[e, oand] = pair;
                const size_t byte_pos = BytePos(token_start);
                if (oand.has_value()) {
                  const auto &[kw, rhs] = oand.value();
                  if (kw.type == ANDALSO) {
                    return pool->Andalso(e, rhs, byte_pos);
                  } else {
                    // This is treated as syntactic sugar, which isn't
                    // ideal.
                    return pool->If(e, rhs, pool->Tuple({}, byte_pos));
                  }
                } else {
                  return e;
                }
              };

          auto OrelseExpr =
            Mark(AndalsoExpr &&
                 Opt((IsToken<ORELSE>() || IsToken<OTHERWISE>()) &&
                     Expr))
            >[&](const auto &pair_pos) {
                const auto &[pair, token_start, token_len] = pair_pos;
                const auto &[e, oor] = pair;
                const size_t byte_pos = BytePos(token_start);
                if (oor.has_value()) {
                  const auto &[kw, rhs] = oor.value();
                  if (kw.type == ORELSE) {
                    return pool->Orelse(e, rhs, byte_pos);
                  } else {
                    return pool->If(e, pool->Tuple({}, byte_pos), rhs);
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
            (WithoutExpr &&
             *(IsToken<WITH>() >>
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
            TypeDecl ||
            LocalDecl(Decl) ||
            OpenDecl(Expr) ||
            ErrorDecl ||
            // Just here for convenience of writing a || b || ...
            Fail<Token, const Dec *>();
        });

  auto Program = Expr << End<Token>();

  auto parseopt = Program(TokenSpan<Token>(tokens.data(), tokens.size()));
  if (!parseopt.HasValue()) return nullptr;
  return parseopt.Value();
}

}  // namespace el
