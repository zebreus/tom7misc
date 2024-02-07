#ifndef _REPHRASE_IL_H
#define _REPHRASE_IL_H

#include <algorithm>
#include <string>
#include <cstdint>
#include <vector>

#include "ast-arena.h"
#include "bignum/big.h"
#include "unification.h"
#include "primop.h"

namespace il {

enum class ExpType {
  STRING,
  FLOAT,
  JOIN,
  RECORD,
  INTEGER,
  VAR,
  LAYOUT,
  LET,
  IF,
  APP,
  FN,
  // Project a field from a record.
  PROJECT,
  // Apply a primop to the types and children.
  PRIMOP,
  // Abort with a string message. Should be
  // replaced with generic exception mechanism.
  FAIL,
};

enum class DecType {
  DO,
  VAL,
};

struct Exp;
struct Dec;

enum class TypeType {
  // Type variables are applied to 0 or more args
  VAR,
  SUM,
  ARROW,
  /* A bundle of mutually-recursive datatypes.
     The idx field projects one of the bundle out.
     Each type in the bundle has its own recursive
     name, which is in scope for all the types.

     pi_n (mu  v_0 . typ_0
           and v_1 . typ_1
           and ...)
       0 <= n < length l, length l > 0.

     when unrolling, choose nth arm and
     substitute:

     typ_n [ (pi_0 mu .. and ..) / v_0,
             (pi_1 mu .. and ..) / v_1,
             ... ] */
  MU,
  RECORD,
  EVAR,
  // Primitive reference type
  REF,
  // There is no "app"; primitive tycons
  // are to be added here.
  STRING,
  FLOAT,
  INT,
};

struct Type {
  TypeType type;
  // XXX private
  Type(TypeType t) : type(t) {}

  const std::vector<std::pair<std::string, const Type *>> &Record() const {
    CHECK(type == TypeType::RECORD);
    return str_children;
  }

  const std::vector<std::pair<std::string, const Type *>> &Sum() const {
    CHECK(type == TypeType::SUM);
    return str_children;
  }

  std::tuple<int, const std::vector<std::pair<std::string, const Type *>> &>
  Mu() const {
    CHECK(type == TypeType::MU);
    return std::tie(idx, str_children);
  }

  std::pair<const Type *, const Type *> Arrow() const {
    CHECK(type == TypeType::ARROW);
    return std::tie(a, b);
  }

  std::pair<const std::string &, const std::vector<const Type *> &>
  Var() const {
    CHECK(type == TypeType::VAR);
    return std::tie(var, children);
  }


  const struct EVar &EVar() const {
    CHECK(type == TypeType::EVAR);
    return evar;
  }

  const Type *Ref() const {
    CHECK(type == TypeType::REF);
    return a;
  }

  void String() const {
    CHECK(type == TypeType::STRING);
  }

  void Float() const {
    CHECK(type == TypeType::FLOAT);
  }

  void Int() const {
    CHECK(type == TypeType::INT);
  }

private:
  friend struct AstPool;
  std::string var;
  const Type *a = nullptr;
  const Type *b = nullptr;
  struct EVar evar;
  std::vector<const Type *> children;
  int idx = 0;
  // For types, these are always sorted by the strings.
  // For sums and products, the strings are the labels.
  // For mu, the strings are bound type variables.
  std::vector<std::pair<std::string, const Type *>> str_children;
};

struct Exp {
  ExpType type;
  // (XXX should be able to make these private?)
  Exp(ExpType t) : type(t) {}

  // Accessors.

  std::tuple<const std::vector<const Type *> &, const std::string &>
  Var() const {
    CHECK(type == ExpType::VAR);
    return std::tie(types, str);
  }

  double Float() const {
    CHECK(type == ExpType::FLOAT);
    return d;
  }

  const BigInt &Integer() const {
    CHECK(type == ExpType::INTEGER);
    return integer;
  }

  const std::string &String() const {
    CHECK(type == ExpType::STRING);
    return str;
  }

  std::tuple<const Exp *, const Exp *> App() const {
    CHECK(type == ExpType::APP);
    return std::tie(a, b);
  }

  const std::vector<const Exp *> &Join() const {
    CHECK(type == ExpType::JOIN);
    return children;
  }

  const std::vector<std::pair<std::string, const Exp *>> &Record() const {
    CHECK(type == ExpType::RECORD);
    return str_children;
  }

  std::tuple<const std::vector<const Dec *> &, const Exp *> Let() const {
    CHECK(type == ExpType::LET);
    return std::tie(decs, a);
  }

  // cond, true, false
  std::tuple<const Exp *, const Exp *, const Exp *> If() const {
    CHECK(type == ExpType::IF);
    return std::tie(a, b, c);
  }

  // self, x, body
  std::tuple<const std::string &, const std::string &, const Exp *>
  Fn() const {
    CHECK(type == ExpType::FN);
    return std::tie(self, str, a);
  }

  std::tuple<const Primop &,
             const std::vector<const Type *> &,
             const std::vector<const Exp *> &>
  Primop() const {
    CHECK(type == ExpType::PRIMOP);
    return std::tie(primop, types, children);
  }

  std::tuple<const std::string &, const Exp *>
  Project() const {
    CHECK(type == ExpType::PROJECT);
    return std::tie(str, a);
  }

  const std::string &Fail() const {
    CHECK(type == ExpType::FAIL);
    return str;
  }

private:
  // PERF: Experiment with std::variant, at least.
  friend struct AstPool;
  std::string str;
  // For recursive functions, the name of the function.
  std::string self;
  BigInt integer;
  enum Primop primop = Primop::REF;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  double d = 0.0;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  std::vector<const Type *> types;
  // Not necessarily sorted: The order here gives the evaluation order.
  std::vector<std::pair<std::string, const Exp *>> str_children;
};

struct Dec {
  DecType type;
  Dec(DecType t) : type(t) {}

  std::tuple<const std::vector<std::string> &,
             const std::string &,
             const Exp *> Val() const {
    CHECK(type == DecType::VAL);
    return std::tie(tyvars, str, exp);
  }

  const Exp *Do() const {
    CHECK(type == DecType::DO);
    return exp;
  }

private:
  friend struct AstPool;
  std::string str;
  std::vector<std::string> tyvars;
  const Exp *exp = nullptr;
};

// The AST pool has constructors for all the IL forms. Each takes
// a final "guess" parameter; if non-null and equal to the object
// that would be allocated, we return the guess instead. This
// helps prevent duplicating lots of syntax nodes
struct AstPool {
  AstPool() = default;

  // Types

  const Type *VarType(const std::string &s,
                      std::vector<const Type *> v = {},
                      const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::VAR &&
        guess->var == s &&
        guess->children == v) {
      return guess;
    }

    Type *ret = NewType(TypeType::VAR);
    ret->var = s;
    ret->children = std::move(v);
    return ret;
  }

  const Type *RecordType(
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::RECORD &&
        guess->str_children == v) {
      return guess;
    }

    Type *ret = NewType(TypeType::RECORD);
    ret->str_children = v;
    SortLabeled(&ret->str_children);
    return ret;
  }

  const Type *SumType(
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::SUM &&
        guess->str_children == v) {
      return guess;
    }

    Type *ret = NewType(TypeType::SUM);
    ret->str_children = std::move(v);
    SortLabeled(&ret->str_children);
    return ret;
  }

  const Type *StringType() {
    return &string_type;
  }

  const Type *FloatType() {
    return &float_type;
  }

  const Type *IntType() {
    return &int_type;
  }

  const Type *RefType(const Type *t, const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::REF &&
        t == guess->a) {
      return guess;
    }

    Type *ret = NewType(TypeType::REF);
    ret->a = t;
    return ret;
  }

  const Type *EVar(EVar a, const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::EVAR &&
        EVar::SameEVar(a, guess->evar)) {
      return guess;
    }

    Type *ret = NewType(TypeType::EVAR);
    ret->evar = std::move(a);
    return ret;
  }

  // Derived form for Record {1: t1, 2: t2, ...}.
  const Type *Product(const std::vector<const Type *> &v,
                      const Type *guess = nullptr);

  const Type *Arrow(const Type *dom, const Type *cod,
                    const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::ARROW &&
        guess->a == dom &&
        guess->b == cod) {
      return guess;
    }

    Type *ret = NewType(TypeType::ARROW);
    ret->a = dom;
    ret->b = cod;
    return ret;
  }


  const Type *Mu(int idx,
                 const std::vector<std::pair<std::string, const Type *>> &v,
                 const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::MU &&
        guess->idx == idx &&
        guess->str_children == v) {
      return guess;
    }

    Type *ret = NewType(TypeType::MU);
    ret->idx = idx;
    ret->str_children = v;
    return ret;
  }


  // Expressions

  const Exp *String(const std::string &s,
                    const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::STRING &&
        guess->str == s) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::STRING);
    ret->str = s;
    return ret;
  }

  const Exp *Float(double d, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::FLOAT &&
        guess->d == d) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::FLOAT);
    ret->d = d;
    return ret;
  }

  const Exp *Var(const std::string &v,
                 std::vector<const Type *> ts = {},
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::VAR &&
        guess->str == v &&
        guess->types == ts) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::VAR);
    ret->str = v;
    ret->types = std::move(ts);
    return ret;
  }

  const Exp *Int(int64_t i, const Exp *guess = nullptr) {
    return Int(BigInt(i), guess);
  }

  const Exp *Int(BigInt i, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::INTEGER &&
        BigInt::Eq(guess->integer, i)) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::INTEGER);
    ret->integer = std::move(i);
    return ret;
  }

  const Exp *Record(
      const std::vector<std::pair<std::string, const Exp *>> &lv,
      const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::RECORD &&
        guess->str_children == lv) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::RECORD);
    ret->str_children = std::move(lv);
    return ret;
  }

  const Exp *Project(std::string s, const Exp *e, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::PROJECT &&
        guess->str == s &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::PROJECT);
    ret->str = std::move(s);
    ret->a = e;
    return ret;
  }

  const Exp *Join(const std::vector<const Exp *> &v,
                  const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::JOIN &&
        guess->children == v) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::JOIN);
    ret->children = v;
    return ret;
  }

  const Exp *Let(const std::vector<const Dec *> &ds,
                 const Exp *e,
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::LET &&
        guess->decs == ds &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::LET);
    ret->decs = ds;
    ret->a = e;
    return ret;
  }

  // Like Let, but flattens d into the declaration list if e
  // is already a LET.
  const Exp *LetFlat(const Dec *d, const Exp *e);

  const Exp *If(const Exp *cond, const Exp *t, const Exp *f,
                const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::IF &&
        guess->a == cond &&
        guess->b == t &&
        guess->c == f) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::IF);
    ret->a = cond;
    ret->b = t;
    ret->c = f;
    return ret;
  }

  const Exp *App(const Exp *f, const Exp *arg,
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::APP &&
        guess->a == f &&
        guess->b == arg) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::APP);
    ret->a = f;
    ret->b = arg;
    return ret;
  }

  const Exp *Primop(Primop po,
                    const std::vector<const Type *> &ts,
                    const std::vector<const Exp *> &es,
                    const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::PRIMOP &&
        guess->primop == po &&
        guess->types == ts &&
        guess->children == es) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::PRIMOP);
    ret->primop = po;
    ret->types = ts;
    ret->children = es;
    return ret;
  }

  // self may be empty to indicate a non-recursive function.
  const Exp *Fn(const std::string &self,
                const std::string &x,
                const Exp *body,
                const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::FN &&
        guess->self == self &&
        guess->str == x &&
        guess->a == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::FN);
    ret->self = self;
    ret->str = x;
    ret->a = body;
    return ret;
  }

  const Exp *Fail(const std::string &msg, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::FAIL &&
        guess->str == msg) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::FAIL);
    ret->str = msg;
    return ret;
  }

  // Declarations

  const Dec *ValDec(const std::vector<std::string> &tyvars,
                    const std::string &v,
                    const Exp *rhs,
                    const Dec *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == DecType::VAL &&
        guess->tyvars == tyvars &&
        guess->str == v &&
        guess->exp == rhs) {
      return guess;
    }

    Dec *ret = NewDec(DecType::VAL);
    ret->tyvars = tyvars;
    ret->str = v;
    ret->exp = rhs;
    return ret;
  }

  const Dec *DoDec(const Exp *e, const Dec *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == DecType::DO &&
        guess->exp == e) {
      return guess;
    }

    Dec *ret = NewDec(DecType::DO);
    ret->exp = e;
    return ret;
  }

  // SubstType(T, v, T') is [T/v]T'
  const Type *SubstType(const Type *t, const std::string &v,
                        const Type *u);

  // Rename x.t to an alpha-equivalent x'.t' with x' fresh.
  const std::pair<std::string, const Type *>
  AlphaVaryType(const std::string &x, const Type *t);

  // Sort labels in the canonical order. Beware that since this
  // is lexicographic, a tuple with 10 or more elements actually
  // has an unintuitive order {"1", "10", "2", "3", ...}.
  template<class T>
  void SortLabeled(std::vector<std::pair<std::string, T>> *v) {
    std::sort(v->begin(), v->end(),
              [&](const auto &a, const auto &b) {
                return a.first < b.first;
              });
  }

  std::string NewVar(std::string hint = "");

 private:
  const Type string_type = Type(TypeType::STRING);
  const Type int_type = Type(TypeType::INT);
  const Type float_type = Type(TypeType::FLOAT);

  const Type *SubstTypeInternal(const Type *t, const std::string &v,
                                const Type *u, bool is_simple);

  Type *NewType(TypeType t) { return type_arena.New(t); }
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Dec *NewDec(DecType t) { return dec_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Dec> dec_arena;
  AstArena<Type> type_arena;
  int next_var = 0;
};

const char *TypeTypeString(const TypeType t);
std::string TypeString(const Type *t);
std::string DecString(const Dec *d);
std::string ExpString(const Exp *e);

}  // namespace il

#endif
