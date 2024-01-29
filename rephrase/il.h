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
  JOIN,
  RECORD,
  INTEGER,
  VAR,
  LAYOUT,
  LET,
  IF,
  APP,
  FN,
  // Apply a primop to the types and children.
  PRIMOP,
};

enum class DecType {
  VAL,
  FUN,
  DATATYPE,
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
  INT,
};

struct Type {
  TypeType type;
  std::string var;
  const Type *a = nullptr;
  const Type *b = nullptr;
  EVar evar;
  std::vector<const Type *> children;
  int idx = 0;
  // For types, these are always sorted by the strings.
  // For sums and products, the strings are the labels.
  // For mu, the strings are bound type variables.
  std::vector<std::pair<std::string, const Type *>> str_children;
  Type(TypeType t) : type(t) {}
};

struct Exp {
  ExpType type;
  Exp(ExpType t) : type(t) {}

  // Accessors.
  /*
    TODO:

  VAR,
  LAYOUT,
  IF,
  // Apply a primop to the types and children.
  PRIMOP,
  */

  std::tuple<const std::vector<const Type *> &, const std::string &>
  Var() const {
    CHECK(type == ExpType::VAR);
    return std::tie(types, str);
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

  std::tuple<const Primop &, const std::vector<const Exp *> &>
  Primop() const {
    CHECK(type == ExpType::PRIMOP);
    return std::tie(primop, children);
  }

private:
  // PERF: Experiment with std::variant, at least.
  friend struct AstPool;
  std::string str;
  // For recursive functions, the name of the function.
  std::string self;
  BigInt integer;
  enum Primop primop;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  std::vector<const Type *> types;
  // Not necessarily sorted: The order here gives the evaluation order.
  std::vector<std::pair<std::string, const Exp *>> str_children;
};

struct Dec {
  DecType type;
  std::string str;
  const Exp *exp = nullptr;
  Dec(DecType t) : type(t) {}
};

struct AstPool {
  AstPool() = default;

  // Types
  const Type *VarType(const std::string &s,
                      std::vector<const Type *> v = {}) {
    Type *ret = NewType(TypeType::VAR);
    ret->var = s;
    ret->children = std::move(v);
    return ret;
  }

  const Type *RecordType(std::vector<std::pair<std::string, const Type *>> v) {
    Type *ret = NewType(TypeType::RECORD);
    ret->str_children = std::move(v);
    SortLabeled(&ret->str_children);
    return ret;
  }

  const Type *SumType(std::vector<std::pair<std::string, const Type *>> v) {
    Type *ret = NewType(TypeType::SUM);
    ret->str_children = std::move(v);
    SortLabeled(&ret->str_children);
    return ret;
  }

  const Type *StringType() {
    return NewType(TypeType::STRING);
  }

  const Type *RefType(const Type *t) {
    Type *ret = NewType(TypeType::REF);
    ret->a = t;
    return ret;
  }

  const Type *IntType() {
    return NewType(TypeType::INT);
  }

  const Type *EVar(EVar a) {
    Type *ret = NewType(TypeType::EVAR);
    ret->evar = std::move(a);
    return ret;
  }

  // Derived form for Record {1: t1, 2: t2, ...}.
  const Type *Product(const std::vector<const Type *> &v);

  const Type *Arrow(const Type *dom, const Type *cod) {
    Type *ret = NewType(TypeType::ARROW);
    ret->a = dom;
    ret->b = cod;
    return ret;
  }

  // Expressions

  const Exp *String(const std::string &s) {
    Exp *ret = NewExp(ExpType::STRING);
    ret->str = s;
    return ret;
  }

  const Exp *Var(const std::string &v,
                 std::vector<const Type *> ts = {}) {
    Exp *ret = NewExp(ExpType::VAR);
    ret->str = v;
    ret->types = std::move(ts);
    return ret;
  }

  const Exp *Int(int64_t i) {
    Exp *ret = NewExp(ExpType::INTEGER);
    ret->integer = BigInt(i);
    return ret;
  }

  const Exp *Int(BigInt i) {
    Exp *ret = NewExp(ExpType::INTEGER);
    ret->integer = std::move(i);
    return ret;
  }

  const Exp *Record(std::vector<std::pair<std::string, const Exp *>> lv) {
    Exp *ret = NewExp(ExpType::RECORD);
    ret->str_children = std::move(lv);
    return ret;
  }

  const Exp *Join(std::vector<const Exp *> v) {
    Exp *ret = NewExp(ExpType::JOIN);
    ret->children = std::move(v);
    return ret;
  }

  const Exp *Let(std::vector<const Dec *> ds, const Exp *e) {
    Exp *ret = NewExp(ExpType::LET);
    ret->decs = std::move(ds);
    ret->a = e;
    return ret;
  }

  const Exp *If(const Exp *cond, const Exp *t, const Exp *f) {
    Exp *ret = NewExp(ExpType::IF);
    ret->a = cond;
    ret->b = t;
    ret->c = f;
    return ret;
  }

  const Exp *App(const Exp *f, const Exp *arg) {
    Exp *ret = NewExp(ExpType::APP);
    ret->a = f;
    ret->b = arg;
    return ret;
  }

  const Exp *Primop(Primop po,
                    std::vector<const Type *> ts,
                    std::vector<const Exp *> es) {
    Exp *ret = NewExp(ExpType::PRIMOP);
    ret->primop = po;
    ret->types = std::move(ts);
    ret->children = std::move(es);
    return ret;
  }

  // self may be empty to indicate a non-recursive function.
  const Exp *Fn(const std::string &self,
                const std::string &x,
                const Exp *body) {
    Exp *ret = NewExp(ExpType::FN);
    ret->self = self;
    ret->str = x;
    ret->a = body;
    return ret;
  }

  // Declarations

  const Dec *ValDec(const std::string &v, const Exp *rhs) {
    Dec *ret = NewDec(DecType::VAL);
    ret->str = v;
    ret->exp = rhs;
    return ret;
  }

  // SubstType(T, v, T') is [T/v]T'
  const Type *SubstType(const Type *t, const std::string &v,
                        const Type *u);

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

private:
  Type *NewType(TypeType t) { return type_arena.New(t); }
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Dec *NewDec(DecType t) { return dec_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Dec> dec_arena;
  AstArena<Type> type_arena;
};

const char *TypeTypeString(const TypeType t);
std::string TypeString(const Type *t);
std::string DecString(const Dec *d);
std::string ExpString(const Exp *e);

}  // namespace ei

#endif
