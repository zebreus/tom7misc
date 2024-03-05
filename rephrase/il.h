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
  INT,
  BOOL,
  VAR,
  // Like var, but not a variable occurrence: A reference to
  // a global symbol.
  GLOBAL_SYM,
  LAYOUT,
  LET,
  IF,
  APP,
  FN,
  // Project a field from a record.
  PROJECT,
  // Construct an element of a sum type.
  INJECT,
  // Construct a recursive type (mu bundle).
  ROLL,
  // ... and the reverse.
  UNROLL,
  // Apply a primop to the types and children.
  PRIMOP,
  // Abort with a string message. Should be
  // replaced with generic exception mechanism.
  FAIL,
  // n-ary sequence
  SEQ,
  // Match against a series of integers.
  INTCASE,
  // ... or strings.
  STRINGCASE,
  // Match against a sum. Binds the same variable
  // for all arms.
  SUMCASE,
  PACK,
  UNPACK,
};

struct Exp;

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
  // ∃α.t. This is only used by closure conversion.
  EXISTS,
  RECORD,
  EVAR,
  // Primitive reference type
  REF,
  // There is no "app"; primitive tycons
  // are to be added here.
  STRING,
  FLOAT,
  INT,
  BOOL,
};

struct Type {
  TypeType type;
  // XXX private
  Type(TypeType t) : type(t) {}

  // Labels are always sorted.
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

  std::tuple<const std::string, const Type *> Exists() const {
    CHECK(type == TypeType::EXISTS);
    return std::tie(var, a);
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

  void Bool() const {
    CHECK(type == TypeType::BOOL);
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
    return std::tie(types, str1);
  }

  std::tuple<const std::vector<const Type *> &, const std::string &>
  GlobalSym() const {
    CHECK(type == ExpType::GLOBAL_SYM);
    return std::tie(types, str1);
  }

  double Float() const {
    CHECK(type == ExpType::FLOAT);
    return d;
  }

  const BigInt &Int() const {
    CHECK(type == ExpType::INT);
    return integer;
  }

  bool Bool() const {
    CHECK(type == ExpType::BOOL);
    return !BigInt::Eq(integer, 0);
  }

  const std::string &String() const {
    CHECK(type == ExpType::STRING);
    return str1;
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

  std::tuple<const std::vector<std::string> &,
             const std::string &,
             const Exp *,
             const Exp *>
  Let() const {
    CHECK(type == ExpType::LET);
    return std::tie(tyvars, str1, a, b);
  }

  // cond, true, false
  std::tuple<const Exp *, const Exp *, const Exp *> If() const {
    CHECK(type == ExpType::IF);
    return std::tie(a, b, c);
  }

  // TODO: Needs arg and return types.
  // self, x, arrow_type, body
  std::tuple<const std::string &, const std::string &, const Type *,
             const Exp *>
  Fn() const {
    CHECK(type == ExpType::FN);
    return std::tie(str2, str1, ta, a);
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
    return std::tie(str1, a);
  }

  std::tuple<const std::string &, const Type *, const Exp *>
  Inject() const {
    CHECK(type == ExpType::INJECT);
    return std::tie(str1, ta, a);
  }

  std::tuple<const Type *, const Exp *>
  Roll() const {
    CHECK(type == ExpType::ROLL);
    return std::tie(ta, a);
  }

  const Exp *Unroll() const {
    CHECK(type == ExpType::UNROLL);
    return a;
  }

  std::tuple<const Exp *, const Type *> Fail() const {
    CHECK(type == ExpType::FAIL);
    return std::tie(a, ta);
  }

  std::tuple<const std::vector<const Exp *> &, const Exp *> Seq() const {
    CHECK(type == ExpType::SEQ);
    return std::tie(children, a);
  }

  std::tuple<const Exp *, const std::vector<std::pair<BigInt, const Exp *>> &,
             const Exp *> IntCase() const {
    CHECK(type == ExpType::INTCASE);
    return std::tie(a, int_children, b);
  }

  std::tuple<const Exp *,
             const std::vector<std::pair<std::string, const Exp *>> &,
             const Exp *> StringCase() const {
    CHECK(type == ExpType::STRINGCASE);
    return std::tie(a, str_children, b);
  }

  std::tuple<
    const Exp *,
    // label, variable, arm
    const std::vector<std::tuple<std::string, std::string, const Exp *>> &,
    const Exp *> SumCase() const {
    CHECK(type == ExpType::SUMCASE);
    return std::tie(a, sumcase_arms, b);
  }

  std::tuple<const Type *, const std::string &, const Type *, const Exp *>
  Pack() const {
    CHECK(type == ExpType::PACK);
    return std::tie(ta, str1, tb, a);
  }

  // unpack alpha, x = rhs in body
  std::tuple<const std::string &, const std::string &, const Exp *, const Exp *>
  Unpack() const {
    CHECK(type == ExpType::UNPACK);
    return std::tie(str1, str2, a, b);
  }

private:
  // PERF: Experiment with std::variant, at least.
  friend struct AstPool;
  std::string str1;
  std::string str2;
  BigInt integer;
  enum Primop primop = Primop::REF;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  const Type *ta = nullptr;
  const Type *tb = nullptr;
  double d = 0.0;
  std::vector<const Exp *> children;
  std::vector<const Type *> types;
  std::vector<std::string> tyvars;
  // Not necessarily sorted: The order here gives the evaluation order.
  std::vector<std::pair<std::string, const Exp *>> str_children;
  // For intcase. The constants must be distinct.
  std::vector<std::pair<BigInt, const Exp *>> int_children;
  // For sumcase. Labels must be distinct.
  std::vector<std::tuple<std::string, std::string, const Exp *>> sumcase_arms;
};

struct Global {
  std::vector<std::string> tyvars;
  std::string sym;
  const Type *type = nullptr;
  const Exp *exp = nullptr;
};

struct Program {
  std::vector<Global> globals;
  const Exp *body = nullptr;
};

// The AST pool has constructors for all the IL forms. Each takes
// a final "guess" parameter; if non-null and equal to the object
// that would be allocated, we return the guess instead. This
// helps prevent duplicating lots of syntax nodes during recursive
// traversals that don't actually change anything.
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

  const Type *BoolType() {
    return &bool_type;
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

  const Type *Exists(
      const std::string &alpha,
      const Type *t,
      const Type *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == TypeType::EXISTS &&
        guess->a == t &&
        guess->var == alpha) {
      return guess;
    }

    Type *ret = NewType(TypeType::EXISTS);
    ret->var = alpha;
    ret->a = t;
    return ret;
  }


  // Expressions

  const Exp *String(const std::string &s,
                    const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::STRING &&
        guess->str1 == s) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::STRING);
    ret->str1 = s;
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

  const Exp *Var(const std::vector<const Type *> &ts,
                 const std::string &v,
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::VAR &&
        guess->str1 == v &&
        guess->types == ts) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::VAR);
    ret->str1 = v;
    ret->types = std::move(ts);
    return ret;
  }

  const Exp *GlobalSym(const std::vector<const Type *> &ts,
                       const std::string &sym,
                       const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::GLOBAL_SYM &&
        guess->str1 == sym &&
        guess->types == ts) {
      return guess;
    }
    CHECK(!sym.empty());
    Exp *ret = NewExp(ExpType::GLOBAL_SYM);
    ret->str1 = sym;
    ret->types = std::move(ts);
    return ret;
  }

  const Exp *Int(int64_t i, const Exp *guess = nullptr) {
    return Int(BigInt(i), guess);
  }

  const Exp *Int(BigInt i, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::INT &&
        BigInt::Eq(guess->integer, i)) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::INT);
    ret->integer = std::move(i);
    return ret;
  }

  const Exp *Bool(bool b, const Exp *guess = nullptr) {
    return b ? &true_exp : &false_exp;
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

  const Exp *Project(const std::string &s, const Exp *e,
                     const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::PROJECT &&
        guess->str1 == s &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::PROJECT);
    ret->str1 = s;
    ret->a = e;
    return ret;
  }

  const Exp *Inject(const std::string &s,
                    const Type *sum_type,
                    const Exp *e,
                    const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::INJECT &&
        guess->str1 == s &&
        guess->ta == sum_type &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::INJECT);
    ret->str1 = s;
    ret->ta = sum_type;
    ret->a = e;
    return ret;
  }

  const Exp *Roll(const Type *t, const Exp *e, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::ROLL &&
        guess->ta == t &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::ROLL);
    ret->ta = t;
    ret->a = e;
    return ret;
  }

  const Exp *Unroll(const Exp *e, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::UNROLL &&
        guess->a == e) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::UNROLL);
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

  const Exp *Let(const std::vector<std::string> &tyvars,
                 const std::string &x,
                 const Exp *rhs,
                 const Exp *body,
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::LET &&
        guess->tyvars == tyvars &&
        guess->str1 == x &&
        guess->a == rhs &&
        guess->b == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::LET);
    ret->tyvars = tyvars;
    ret->str1 = x;
    ret->a = rhs;
    ret->b = body;
    return ret;
  }

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
                const Type *arrow_type,
                const Exp *body,
                const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::FN &&
        guess->str2 == self &&
        guess->str1 == x &&
        guess->ta == arrow_type &&
        guess->a == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::FN);
    ret->str2 = self;
    ret->str1 = x;
    ret->ta = arrow_type;
    ret->a = body;
    return ret;
  }

  // t is the return type of the fail (which doesn't actually return)
  const Exp *Fail(const Exp *msg, const Type *t, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::FAIL &&
        guess->a == msg &&
        guess->ta == t) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::FAIL);
    ret->a = msg;
    ret->ta = t;
    return ret;
  }

  const Exp *Seq(const std::vector<const Exp *> &v,
                 const Exp *body,
                 const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::SEQ &&
        guess->children == v &&
        guess->a == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::SEQ);
    ret->children = v;
    ret->a = body;
    return ret;
  }

  const Exp *IntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::INTCASE &&
        guess->a == obj &&
        guess->b == def &&
        IntChildrenEq(guess->int_children, arms)) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::INTCASE);
    ret->a = obj;
    ret->int_children = arms;
    ret->b = def;
    return ret;
  }

  const Exp *StringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::STRINGCASE &&
        guess->a == obj &&
        guess->b == def &&
        guess->str_children == arms) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::STRINGCASE);
    ret->a = obj;
    ret->str_children = arms;
    ret->b = def;
    return ret;
  }

  const Exp *SumCase(
      const Exp *obj,
      const std::vector<
          // label, variable, arm
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::SUMCASE &&
        guess->a == obj &&
        guess->b == def &&
        guess->sumcase_arms == arms) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::SUMCASE);
    ret->a = obj;
    ret->sumcase_arms = arms;
    ret->b = def;
    return ret;
  }

  const Exp *Pack(const Type *t_hidden, const std::string &alpha,
                  const Type *t_packed, const Exp *body,
                  const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::PACK &&
        guess->ta == t_hidden &&
        guess->str1 == alpha &&
        guess->tb == t_packed &&
        guess->a == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::PACK);
    ret->ta = t_hidden;
    ret->str1 = alpha;
    ret->tb = t_packed;
    ret->a = body;
    return ret;
  }


  const Exp *Unpack(
      const std::string &alpha, const std::string &x, const Exp *rhs,
      const Exp *body, const Exp *guess = nullptr) {
    if (guess != nullptr &&
        guess->type == ExpType::UNPACK &&
        guess->str1 == alpha &&
        guess->str2 == x &&
        guess->a == rhs &&
        guess->b == body) {
      return guess;
    }

    Exp *ret = NewExp(ExpType::UNPACK);
    ret->str1 = alpha;
    ret->str2 = x;
    ret->a = rhs;
    ret->b = body;
    return ret;
  }

  // Utilities.


  const Type *UnrollType(const Type *mu);

  // SubstType(T, v, T') is [T/v]T'
  const Type *SubstType(const Type *t, const std::string &v,
                        const Type *u);

  // TODO: Move to il-util with its friends?
  // Rename x.t to an alpha-equivalent x'.t' with x' fresh.
  const std::pair<std::string, const Type *>
  AlphaVaryType(const std::string &x, const Type *t);

  std::pair<std::vector<std::string>, std::vector<const Type *>>
  AlphaVaryMultipleTypes(
      const std::vector<std::string> &xv,
      const std::vector<const Type *> &tv);

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

  // Create a new variable.
  std::string NewVar(const std::string &hint = "");
  // Get an unspecified string from a variable;
  // typically BaseVar(NewVar(s)) == s.
  std::string BaseVar(const std::string &x);

 private:
  const Type string_type = Type(TypeType::STRING);
  const Type int_type = Type(TypeType::INT);
  const Type float_type = Type(TypeType::FLOAT);
  const Type bool_type = Type(TypeType::BOOL);

  const Exp true_exp = []() {
      Exp t(ExpType::BOOL);
      t.integer = BigInt(1);
      return t;
    }();
  const Exp false_exp = []() {
      Exp t(ExpType::BOOL);
      t.integer = BigInt(0);
      return t;
    }();

  const Type *SubstTypeInternal(const Type *t, const std::string &v,
                                const Type *u, bool is_simple);

  // Could do this with big-overloads but probably better avoid polluting
  // the global namespace in a header.
  bool IntChildrenEq(const std::vector<std::pair<BigInt, const Exp *>> &a,
                     const std::vector<std::pair<BigInt, const Exp *>> &b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < (int)a.size(); i++) {
      if (a[i].second != b[i].second) return false;
      if (!BigInt::Eq(a[i].first, b[i].first)) return false;
    }
    return true;
  }

  Type *NewType(TypeType t) { return type_arena.New(t); }
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Type> type_arena;
  int next_var = 0;
};

const char *TypeTypeString(const TypeType t);
std::string TypeString(const Type *t);
std::string ExpString(const Exp *e);
std::string ProgramString(const Program &pgm);

}  // namespace il

#endif
